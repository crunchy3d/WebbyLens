#include "scrcpy_client.h"
#include "log.h"
#include <ws2tcpip.h>
#include <random>
#include <sstream>

static const wchar_t* kDeviceServerPath = L"/data/local/tmp/webbylens-server.jar";

static UINT32 BE32(const BYTE* p) { return (UINT32(p[0]) << 24) | (UINT32(p[1]) << 16) | (UINT32(p[2]) << 8) | p[3]; }
static UINT64 BE64(const BYTE* p) { return (UINT64(BE32(p)) << 32) | BE32(p + 4); }

ScrcpyClient::ScrcpyClient()
{
    // <program folder>\platform-tools\adb.exe (downloaded by the installer from Google),
    // otherwise an adb.exe found on the PATH
    _adb = ExeDir() + L"\\platform-tools\\adb.exe";
    if (GetFileAttributesW(_adb.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        wchar_t found[MAX_PATH];
        if (SearchPathW(nullptr, L"adb.exe", nullptr, MAX_PATH, found, nullptr))
            _adb = found;
    }
    _serverJar = ExeDir() + L"\\scrcpy-server";
}

ScrcpyClient::~ScrcpyClient()
{
    Disconnect();
}

bool ScrcpyClient::Adb(const std::wstring& args, std::string* out, DWORD timeoutMs)
{
    std::wstring cmd = L"\"" + _adb + L"\" " + args;
    DWORD code = 1;
    std::string o;
    // the adb daemon started by "start-server" must survive WebbyLens (it keeps the phone authorized/ready)
    bool detached = args == L"start-server";
    bool ok = RunCapture(cmd, &o, &code, timeoutMs, detached) && code == 0;
    if (out) *out = o;
    return ok;
}

bool ScrcpyClient::FindDevice(const std::wstring& wanted, std::string* error)
{
    std::string out;
    if (!Adb(L"devices", &out))
    {
        *error = "adb is not working (" + WideToUtf8(_adb) + "): " + out;
        return false;
    }

    std::istringstream lines(out);
    std::string line;
    std::string unauthorized, offline;
    _serial.clear();
    while (std::getline(lines, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string serial = line.substr(0, tab), state = line.substr(tab + 1);
        if (!wanted.empty() && Utf8ToWide(serial) != wanted) continue;
        if (state == "device")
        {
            _serial = Utf8ToWide(serial);
            return true;
        }
        if (state == "unauthorized") unauthorized = serial;
        else offline = serial;
    }
    if (!unauthorized.empty())
        *error = "Phone " + unauthorized + " is not authorized: unlock it and accept the 'Allow USB debugging' prompt.";
    else if (!offline.empty())
        *error = "Phone " + offline + " is offline: reconnect the USB cable.";
    else
        *error = "No phone found. Connect it by USB and enable USB debugging.";
    return false;
}

bool ScrcpyClient::PushServer(std::string* error)
{
    std::string out;
    if (!Adb(L"-s " + _serial + L" push \"" + _serverJar + L"\" " + kDeviceServerPath, &out, 30000))
    {
        *error = "Could not copy scrcpy-server to the phone: " + out;
        return false;
    }
    return true;
}

bool ScrcpyClient::RecvAll(SOCKET s, void* buf, int len)
{
    char* p = static_cast<char*>(buf);
    while (len > 0)
    {
        int n = recv(s, p, len, 0);
        if (n <= 0)
            return false;
        p += n;
        len -= n;
    }
    return true;
}

static std::wstring ServerArgs(const StreamOptions& opt, UINT32 scid, bool list)
{
    std::ostringstream a;
    a << "CLASSPATH=/data/local/tmp/webbylens-server.jar app_process / com.genymobile.scrcpy.Server " SCRCPY_VERSION;
    char hex[16];
    sprintf_s(hex, "%08x", scid);
    a << " scid=" << hex << " log_level=info";
    a << " video_source=camera";
    // control channel only for camera zoom; never sync the clipboard or touch the phone screen
    if (!list && opt.video && opt.control)
        a << " control=true clipboard_autosync=false power_on=false";
    else
        a << " control=false";
    if (!opt.cameraId.empty()) a << " camera_id=" << opt.cameraId;
    else if (!opt.facing.empty()) a << " camera_facing=" << opt.facing;
    if (list)
    {
        a << " audio=false list_cameras=true list_camera_sizes=true video=false";
        return Utf8ToWide(a.str());
    }
    if (opt.audio)
        a << " audio=true audio_codec=raw audio_source=" << opt.audioSource;
    else
        a << " audio=false";
    a << " tunnel_forward=true";
    if (!opt.video)
        return Utf8ToWide(a.str() + " video=false");   // sound only: the camera is not opened
    a << " video_codec=h264";
    a << " video_bit_rate=" << opt.bitRate;
    if (!opt.size.empty()) a << " camera_size=" << opt.size;
    else
    {
        if (opt.maxSize > 0) a << " max_size=" << opt.maxSize;
        if (!opt.aspect.empty()) a << " camera_ar=" << opt.aspect;
    }
    if (opt.fps > 0) a << " camera_fps=" << opt.fps << " max_fps=" << opt.fps;
    if (!opt.orientation.empty()) a << " capture_orientation=" << opt.orientation;
    if (opt.zoom > 0)
    {
        // scrcpy ignores exactly 1.0 (leaves the lens choice to the phone): nudge it so it is applied.
        float z = opt.zoom == 1.0f ? 1.001f : opt.zoom;
        char zs[32];
        sprintf_s(zs, "%.3f", z);
        a << " camera_zoom=" << zs;
    }
    return Utf8ToWide(a.str());
}

std::string ScrcpyClient::HandshakeError() const
{
    if (_cameraBusy)
        return "The phone camera is used by another app on the phone (e.g. the Camera app) - close it.";
    return "Connection closed during handshake. See WebbyLens.log.";
}

bool ScrcpyClient::ListCameras(const StreamOptions& opt, std::string* output, std::string* error)
{
    if (GetFileAttributesW(_adb.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        *error = "adb.exe not found (expected " + WideToUtf8(_adb) + ")";
        return false;
    }
    Adb(L"start-server", nullptr, 20000);
    if (!FindDevice(opt.serial, error) || !PushServer(error))
        return false;
    StreamOptions o = opt;
    o.facing.clear();
    o.cameraId.clear();
    std::string out;
    Adb(L"-s " + _serial + L" shell " + ServerArgs(o, 0x1234, true), &out, 30000);
    *output = out;
    return true;
}

bool ScrcpyClient::Connect(const StreamOptions& requested, std::string* error)
{
    StreamOptions opt = requested;
    Disconnect();
    _cameraBusy = false;

    if (GetFileAttributesW(_adb.c_str()) == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(_serverJar.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        *error = "adb.exe or scrcpy-server missing in " + WideToUtf8(ExeDir()) + " - please reinstall WebbyLens";
        return false;
    }

    Adb(L"start-server", nullptr, 20000);
    if (!FindDevice(opt.serial, error))
        return false;
    if (_onDevice)
        _onDevice(_serial, &opt);   // e.g. the settings remembered for this phone
    _opt = opt;
    if (!PushServer(error))
        return false;

    std::random_device rd;
    UINT32 scid = rd() & 0x7FFFFFFF;
    wchar_t socketName[32];
    swprintf_s(socketName, L"scrcpy_%08x", scid);

    // adb forward: we connect to localhost:port, adb relays to the server socket on the phone
    _port = 0;
    for (int port = 27183; port <= 27199 && !_port; port++)
    {
        wchar_t args[128];
        swprintf_s(args, L"-s %s forward tcp:%d localabstract:%s", _serial.c_str(), port, socketName);
        if (Adb(args, nullptr))
            _port = port;
    }
    if (!_port)
    {
        *error = "Could not create an adb tunnel (ports 27183-27199 busy?)";
        return false;
    }

    std::wstring cmd = L"\"" + _adb + L"\" -s " + _serial + L" shell " + ServerArgs(opt, scid, false);
    LogLine("Starting server: " + WideToUtf8(cmd));
    if (!_server.Start(cmd, [this](const std::string& line) {
            LogLine("[phone] " + line);
            if (line.find("CAMERA_IN_USE") != std::string::npos || line.find("CAMERA_DISABLED") != std::string::npos)
                _cameraBusy = true;
            if (_onServerLine) _onServerLine(line);
        }))
    {
        *error = "Could not start adb shell";
        return false;
    }

    // The server needs a moment to start listening: retry until the dummy byte arrives.
    bool ok = false;
    for (int attempt = 0; attempt < 50 && !ok; attempt++)
    {
        if (!_server.IsRunning())
            break;
        _sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((u_short)_port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(_sock, (sockaddr*)&addr, sizeof(addr)) == 0)
        {
            DWORD tmo = 2000;
            setsockopt(_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmo, sizeof(tmo));
            char dummy;
            if (recv(_sock, &dummy, 1, 0) == 1)
            {
                ok = true;
                break;
            }
        }
        closesocket(_sock);
        _sock = INVALID_SOCKET;
        Sleep(200);
    }

    // The first connection is the video one; the server accepts the audio connection right
    // after it (no dummy byte). In sound-only mode the first connection is the audio one.
    _audioNote.clear();
    SOCKET first = _sock;
    if (ok && !opt.video)
    {
        _audioSock = _sock;
        _sock = INVALID_SOCKET;
    }
    else if (ok && opt.audio)
    {
        _audioSock = ConnectLocal(_port);
        if (_audioSock == INVALID_SOCKET)
            _audioNote = "could not open the audio connection";
    }
    // third connection: control (video mode only), used for live zoom
    if (ok && opt.video && opt.control)
        _controlSock = ConnectLocal(_port);

    wchar_t rm[128];
    swprintf_s(rm, L"-s %s forward --remove tcp:%d", _serial.c_str(), _port);
    Adb(rm, nullptr);

    if (!ok)
    {
        *error = "The phone did not answer (camera busy, or Android older than 12?). See WebbyLens.log.";
        Disconnect();
        return false;
    }

    // device name (64 bytes) on the first connection
    BYTE meta[64];
    if (!RecvAll(first, meta, sizeof(meta)))
    {
        *error = HandshakeError();
        Disconnect();
        return false;
    }
    meta[63] = 0;
    _deviceName = reinterpret_cast<char*>(meta);

    if (opt.video)
    {
        BYTE codec[4];
        if (!RecvAll(codec, sizeof(codec)))
        {
            *error = HandshakeError();
            Disconnect();
            return false;
        }
        UINT32 codecId = BE32(codec);
        if (codecId == 0 || codecId == 1)
        {
            *error = "The phone could not start the camera stream. See WebbyLens.log.";
            Disconnect();
            return false;
        }
        if (codecId != 0x68323634) // "h264"
        {
            *error = "Unexpected video codec from the phone";
            Disconnect();
            return false;
        }
    }

    // Audio stream header: codec id. 0 = the phone disabled audio (e.g. no permission), 1 = error.
    if (_audioSock != INVALID_SOCKET)
    {
        DWORD tmo5 = 5000;
        setsockopt(_audioSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmo5, sizeof(tmo5));
        BYTE acodec[4];
        UINT32 aid = RecvAll(_audioSock, acodec, 4) ? BE32(acodec) : 0xFFFFFFFF;
        if (aid != 0x00726177) // "raw"
        {
            _audioNote = aid == 0 ? "the phone could not capture the microphone"
                       : aid == 1 ? "audio configuration error on the phone"
                       : "no audio stream from the phone";
            closesocket(_audioSock);
            _audioSock = INVALID_SOCKET;
        }
        else
        {
            DWORD none = 0;
            setsockopt(_audioSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&none, sizeof(none));
        }
    }
    if (!opt.video && _audioSock == INVALID_SOCKET)
    {
        *error = "Sound only: " + (_audioNote.empty() ? std::string("no audio from the phone") : _audioNote);
        Disconnect();
        return false;
    }

    // From now on block indefinitely (the camera may pause), Disconnect() unblocks us.
    DWORD tmo = 0;
    setsockopt(_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmo, sizeof(tmo));
    BOOL nodelay = TRUE;
    setsockopt(_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
    _connected = true;
    return true;
}

SOCKET ScrcpyClient::ConnectLocal(int port)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return s;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0)
    {
        closesocket(s);
        return INVALID_SOCKET;
    }
    BOOL nodelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
    return s;
}

bool ScrcpyClient::ReadAudioPacket(AudioPacket* p)
{
    SOCKET s = _audioSock;
    if (s == INVALID_SOCKET)
        return false;
    BYTE hdr[12];
    for (;;)
    {
        if (!RecvAll(s, hdr, sizeof(hdr)))
            return false;
        UINT64 ptsFlags = BE64(hdr);
        UINT32 size = BE32(hdr + 8);
        if (size > 1024 * 1024)
            return false;
        p->data.resize(size);
        if (size && !RecvAll(s, p->data.data(), (int)size))
            return false;
        if ((ptsFlags >> 62) & 1)
            continue;   // config packet (not used for raw audio)
        p->pts = (INT64)(ptsFlags & ((1ULL << 61) - 1));
        return true;
    }
}

bool ScrcpyClient::ReadPacket(VideoPacket* p)
{
    BYTE hdr[12];
    if (!RecvAll(hdr, sizeof(hdr)))
        return false;

    p->data.clear();
    p->session = (hdr[0] & 0x80) != 0;
    if (p->session)
    {
        p->width = BE32(hdr + 4);
        p->height = BE32(hdr + 8);
        p->config = p->keyFrame = false;
        return true;
    }

    UINT64 ptsFlags = BE64(hdr);
    UINT32 size = BE32(hdr + 8);
    p->config = (ptsFlags >> 62) & 1;
    p->keyFrame = (ptsFlags >> 61) & 1;
    p->pts = (INT64)(ptsFlags & ((1ULL << 61) - 1));
    if (size > 32 * 1024 * 1024)
        return false;
    p->data.resize(size);
    return size == 0 || RecvAll(p->data.data(), (int)size);
}

bool ScrcpyClient::SendZoom(bool in)
{
    SOCKET s = _controlSock;
    if (s == INVALID_SOCKET || !_connected)
        return false;
    char msg = in ? 19 : 20;   // TYPE_CAMERA_ZOOM_IN / TYPE_CAMERA_ZOOM_OUT (scrcpy 5.0.1)
    return send(s, &msg, 1, 0) == 1;
}

void ScrcpyClient::Interrupt()
{
    for (SOCKET s : { _sock, _audioSock, _controlSock })
        if (s != INVALID_SOCKET)
            shutdown(s, SD_BOTH);
}

// The audio reader thread must be stopped (Interrupt + join) before Disconnect closes its socket.
void ScrcpyClient::Disconnect()
{
    _connected = false;
    Interrupt();
    if (_sock != INVALID_SOCKET)
    {
        closesocket(_sock);
        _sock = INVALID_SOCKET;
    }
    if (_audioSock != INVALID_SOCKET)
    {
        closesocket(_audioSock);
        _audioSock = INVALID_SOCKET;
    }
    if (_controlSock != INVALID_SOCKET)
    {
        closesocket(_controlSock);
        _controlSock = INVALID_SOCKET;
    }
    _server.Kill();
}
