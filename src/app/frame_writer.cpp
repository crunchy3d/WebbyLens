#include "frame_writer.h"
#include "log.h"
#include "process.h"
#include <mfapi.h>
#include <mfidl.h>
#include <aclapi.h>
#include <algorithm>
#include <cmath>
#include <vector>

bool FrameWriter::Open(std::string* error)
{
    wchar_t path[MAX_PATH];
    if (!WebbyLensSharedPath(path, MAX_PATH, true))
    {
        *error = "Cannot build the shared file path";
        return false;
    }

    // TEMPORARY: keep the data in the file cache, avoid writing it to disk.
    _file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE | READ_CONTROL | WRITE_DAC,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr);
    bool canSetAcl = _file != INVALID_HANDLE_VALUE;
    if (!canSetAcl)
    {
        // not the owner of the file (e.g. created by an elevated WebbyLens): open without WRITE_DAC
        _file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr);
    }
    if (_file == INVALID_HANDLE_VALUE)
    {
        *error = "Cannot open shared file " + WideToUtf8(path) + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    if (canSetAcl)
        GrantCameraServiceWrite();

    _mapping = CreateFileMappingW(_file, nullptr, PAGE_READWRITE, 0, WEBBYLENS_FILE_BYTES, nullptr);
    if (!_mapping)
    {
        *error = "CreateFileMapping failed (error " + std::to_string(GetLastError()) + ")";
        Close();
        return false;
    }
    _view = MapViewOfFile(_mapping, FILE_MAP_WRITE, 0, 0, WEBBYLENS_FILE_BYTES);
    if (!_view)
    {
        *error = "MapViewOfFile failed (error " + std::to_string(GetLastError()) + ")";
        Close();
        return false;
    }

    _hdr = static_cast<WebbyLensShared*>(_view);
    _hdr->magic = WEBBYLENS_SHARED_MAGIC;
    _hdr->version = WEBBYLENS_SHARED_VERSION;
    _hdr->slotBytes = WEBBYLENS_SLOT_BYTES;
    _hdr->slotCount = WEBBYLENS_SLOT_COUNT;
    _hdr->hostPid = GetCurrentProcessId();
    InterlockedExchange(&_hdr->latest, -1);
    InterlockedExchange64(&_hdr->heartbeat, 0);
    InterlockedExchange(&_hdr->paused, 0);
    for (auto& s : _hdr->slots)
        if (s.seq & 1) InterlockedIncrement64(&s.seq);   // repair after a crash mid-write
    return true;
}

void FrameWriter::Write(const Nv12Frame& f)
{
    if (!_hdr) return;

    UINT32 w = f.width & ~1u, h = f.height & ~1u;
    if (!w || !h)
        return;

    // Too many pixels for a slot (e.g. a 1920x1920 camera mode): shrink, keeping the aspect ratio.
    if ((size_t)w * h > WEBBYLENS_MAX_PIXELS)
    {
        double s = sqrt((double)WEBBYLENS_MAX_PIXELS / ((double)w * h));
        UINT32 nw = (UINT32)(w * s) & ~1u, nh = (UINT32)(h * s) & ~1u;
        if (!_scaledNotice)
        {
            LogLine("frame " + std::to_string(w) + "x" + std::to_string(h) + " too large, scaling to " +
                    std::to_string(nw) + "x" + std::to_string(nh));
            _scaledNotice = true;
        }
        _scaled.resize((size_t)nw * nh * 3 / 2);
        _rsY.Plane(f.y, (int)w, (int)h, f.pitch, _scaled.data(), (int)nw, (int)nh, (LONG)nw, 1);
        _rsUV.Plane(f.uv, (int)w / 2, (int)h / 2, f.pitch, _scaled.data() + (size_t)nw * nh, (int)nw / 2, (int)nh / 2, (LONG)nw, 2);
        Nv12Frame g{ _scaled.data(), _scaled.data() + (size_t)nw * nh, (LONG)nw, nw, nh, f.pts };
        Write(g);
        return;
    }

    LONG latest = _hdr->latest;
    int slot = latest < 0 ? 0 : (latest + 1) % WEBBYLENS_SLOT_COUNT;
    WebbyLensSlot& s = _hdr->slots[slot];
    BYTE* dst = WebbyLensSlotData(_view, slot);

    InterlockedIncrement64(&s.seq);           // odd: writing
    s.width = w;
    s.height = h;
    for (UINT32 y = 0; y < h; y++)
        memcpy(dst + (size_t)y * w, f.y + (size_t)y * f.pitch, w);
    BYTE* dstUV = dst + (size_t)w * h;
    for (UINT32 y = 0; y < h / 2; y++)
        memcpy(dstUV + (size_t)y * w, f.uv + (size_t)y * f.pitch, w);
    LONG64 now = MFGetSystemTime();
    s.timestamp = now;
    InterlockedIncrement64(&s.seq);           // even: complete
    InterlockedExchange(&_hdr->latest, slot);
    InterlockedExchange64(&_hdr->heartbeat, now);
}

// The camera (inside the Windows Frame Server, account LOCAL SERVICE) writes a "frames are
// being watched" timestamp into the header; by default it could only read the file.
void FrameWriter::GrantCameraServiceWrite()
{
    BYTE sid[SECURITY_MAX_SID_SIZE];
    DWORD sidSize = sizeof(sid);
    if (!CreateWellKnownSid(WinLocalServiceSid, nullptr, sid, &sidSize))
        return;
    PACL oldDacl = nullptr, newDacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetSecurityInfo(_file, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldDacl, nullptr, &sd) != ERROR_SUCCESS)
        return;
    EXPLICIT_ACCESSW ea{};
    ea.grfAccessPermissions = GENERIC_READ | GENERIC_WRITE;
    ea.grfAccessMode = GRANT_ACCESS;
    ea.grfInheritance = NO_INHERITANCE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);
    if (SetEntriesInAclW(1, &ea, oldDacl, &newDacl) == ERROR_SUCCESS)
    {
        DWORD r = SetSecurityInfo(_file, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, newDacl, nullptr);
        if (r != ERROR_SUCCESS)
            LogLine("shared file: cannot grant camera write access (" + std::to_string(r) + ")");
        LocalFree(newDacl);
    }
    LocalFree(sd);
}

bool FrameWriter::ConsumerActive() const
{
    if (!_hdr) return false;
    LONG64 beat = _hdr->consumerBeat;
    return beat && MFGetSystemTime() - beat < WEBBYLENS_CONSUMER_ACTIVE_100NS;
}

void FrameWriter::SetPaused(bool paused)
{
    if (_hdr)
    {
        InterlockedExchange(&_hdr->paused, paused ? 1 : 0);
        if (paused)
            SetNoSignal();
    }
}

void FrameWriter::SetNoSignal()
{
    if (_hdr)
    {
        InterlockedExchange64(&_hdr->heartbeat, 0);
        InterlockedExchange(&_hdr->latest, -1);
    }
}

void FrameWriter::Close()
{
    if (_hdr)
    {
        InterlockedExchange64(&_hdr->heartbeat, 0);   // tell the camera: no signal
        InterlockedExchange(&_hdr->paused, 0);
        InterlockedExchange(&_hdr->latest, -1);
        _hdr = nullptr;
    }
    if (_view) { UnmapViewOfFile(_view); _view = nullptr; }
    if (_mapping) { CloseHandle(_mapping); _mapping = nullptr; }
    if (_file != INVALID_HANDLE_VALUE) { CloseHandle(_file); _file = INVALID_HANDLE_VALUE; }
}
