// Connection to the scrcpy server (v5.0.1 protocol) over an adb forward tunnel.
#pragma once
#include <winsock2.h>
#include <windows.h>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include "process.h"

#define SCRCPY_VERSION "5.0.1"

struct StreamOptions
{
    std::wstring serial;            // empty = first device
    std::string facing = "front";   // front | back | external
    std::string cameraId;           // overrides facing
    std::string size;               // e.g. "1920x1080", empty = automatic
    std::string aspect = "16:9";    // used when size is empty: 16:9, 4:3, sensor, ...
    std::string orientation;        // capture_orientation: 0, 90, 180, 270, flip0, ...
    int fps = 30;
    float zoom = 0;                 // 0 = phone default; e.g. 1.0 = main lens, 0.5 = ultra-wide
    int maxSize = 1920;
    int bitRate = 8000000;
    bool video = true;              // false = sound only
    bool control = true;            // control channel (used only for live camera zoom)
    bool audio = true;              // phone microphone, raw 48 kHz stereo s16
    std::string audioSource = "mic"; // mic, mic-voice-communication, mic-unprocessed, ...
};

struct AudioPacket
{
    INT64 pts = 0;          // microseconds
    std::vector<BYTE> data; // 48 kHz, 2 channels, 16-bit little endian
};

struct VideoPacket
{
    bool session = false;   // session packet: new width/height
    bool config = false;    // codec config (SPS/PPS)
    bool keyFrame = false;
    INT64 pts = 0;          // microseconds
    UINT32 width = 0, height = 0;
    std::vector<BYTE> data;
};

class ScrcpyClient
{
public:
    ScrcpyClient();
    ~ScrcpyClient();

    // Finds the phone, starts the server and connects the video socket.
    // On failure returns false and a user-readable reason.
    bool Connect(const StreamOptions& opt, std::string* error);
    bool ReadPacket(VideoPacket* packet);
    bool HasAudio() const { return _audioSock != INVALID_SOCKET; }
    bool HasControl() const { return _controlSock != INVALID_SOCKET; }
    bool SendZoom(bool in);     // one zoom step (factor 1.0625) on the running camera
    // Receives every line printed by the phone-side server (e.g. "Set camera zoom: 1.13").
    void SetServerLineHandler(std::function<void(const std::string&)> fn) { _onServerLine = std::move(fn); }
    // Called once the phone is identified, before the server starts: may adjust the options.
    using DeviceHook = std::function<void(const std::wstring& serial, StreamOptions* opt)>;
    void SetDeviceHook(DeviceHook fn) { _onDevice = std::move(fn); }
    const StreamOptions& Options() const { return _opt; }   // options of the current connection
    bool ReadAudioPacket(AudioPacket* packet);   // call from a separate thread
    void Disconnect();
    void Interrupt();           // unblocks ReadPacket from another thread (Ctrl+C)
    const std::string& DeviceName() const { return _deviceName; }
    const std::wstring& Serial() const { return _serial; }

    // Lists cameras via the server (prints server output).
    bool ListCameras(const StreamOptions& opt, std::string* output, std::string* error);

private:
    bool Adb(const std::wstring& args, std::string* out, DWORD timeoutMs = 15000);
    bool FindDevice(const std::wstring& wanted, std::string* error);
    bool PushServer(std::string* error);
    bool RecvAll(void* buf, int len) { return RecvAll(_sock, buf, len); }
    static bool RecvAll(SOCKET s, void* buf, int len);
    static SOCKET ConnectLocal(int port);
    std::string HandshakeError() const;
    std::atomic<bool> _cameraBusy{ false };   // phone reported CAMERA_IN_USE

    std::wstring _adb;
    std::wstring _serverJar;
    std::wstring _serial;
    std::string _deviceName;
    int _port = 0;
    SOCKET _sock = INVALID_SOCKET;
    SOCKET _audioSock = INVALID_SOCKET;
    SOCKET _controlSock = INVALID_SOCKET;
    std::function<void(const std::string&)> _onServerLine;
    DeviceHook _onDevice;
    StreamOptions _opt;
    std::string _audioNote;      // why audio is unavailable (if it is)
public:
    const std::string& AudioNote() const { return _audioNote; }
private:
    ChildProcess _server;
    std::atomic<bool> _connected{ false };
};
