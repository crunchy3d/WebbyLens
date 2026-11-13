// Reads the newest phone frame from the shared file written by WebbyLens.exe
// and scales it (bilinear, aspect-preserving with black bars) into the
// format requested by the camera client.
#include "common.h"
#include "../common/shared_frame.h"
#include <vector>

namespace
{
    void FillRect(BYTE* p, LONG pitch, int x, int y, int w, int h, int ch, const BYTE* value)
    {
        for (int yy = y; yy < y + h; yy++)
        {
            BYTE* row = p + (LONG_PTR)yy * pitch + (LONG_PTR)x * ch;
            if (ch == 1) memset(row, value[0], w);
            else for (int xx = 0; xx < w; xx++) { row[xx * 2] = value[0]; row[xx * 2 + 1] = value[1]; }
        }
    }

    // NV12 (sw x sh, stride sw) -> NV12 (dw x dh) letterboxed into dstY/dstUV
    void ScaleNv12Fit(Resampler& ry_, Resampler& ruv, const BYTE* src, int sw, int sh,
                      BYTE* dstY, BYTE* dstUV, LONG pitch, int dw, int dh)
    {
        int rw, rh;
        if ((LONGLONG)sw * dh > (LONGLONG)dw * sh) { rw = dw; rh = (int)((LONGLONG)dw * sh / sw); }
        else { rh = dh; rw = (int)((LONGLONG)dh * sw / sh); }
        rw &= ~1; rh &= ~1;
        if (rw < 2) rw = 2;
        if (rh < 2) rh = 2;
        int rx = ((dw - rw) / 2) & ~1, ry = ((dh - rh) / 2) & ~1;

        const BYTE black[2] = { 16, 0 };
        const BYTE grey[2] = { 128, 128 };
        if (ry > 0)
        {
            FillRect(dstY, pitch, 0, 0, dw, ry, 1, black);
            FillRect(dstY, pitch, 0, ry + rh, dw, dh - ry - rh, 1, black);
            FillRect(dstUV, pitch, 0, 0, dw / 2, ry / 2, 2, grey);
            FillRect(dstUV, pitch, 0, (ry + rh) / 2, dw / 2, (dh - ry - rh) / 2, 2, grey);
        }
        if (rx > 0)
        {
            FillRect(dstY, pitch, 0, ry, rx, rh, 1, black);
            FillRect(dstY, pitch, rx + rw, ry, dw - rx - rw, rh, 1, black);
            FillRect(dstUV, pitch, 0, ry / 2, rx / 2, rh / 2, 2, grey);
            FillRect(dstUV, pitch, (rx + rw) / 2, ry / 2, (dw - rx - rw) / 2, rh / 2, 2, grey);
        }

        ry_.Plane(src, sw, sh, sw, dstY + (LONG_PTR)ry * pitch + rx, rw, rh, pitch, 1);
        ruv.Plane(src + (size_t)sw * sh, sw / 2, sh / 2, sw,
                  dstUV + (LONG_PTR)(ry / 2) * pitch + rx, rw / 2, rh / 2, pitch, 2);
    }

    inline BYTE Clip(int v) { return (BYTE)(v < 0 ? 0 : v > 255 ? 255 : v); }

    void Nv12ToRgb32(const BYTE* y, const BYTE* uv, LONG srcPitch, BYTE* dst, LONG dstPitch, int w, int h)
    {
        for (int r = 0; r < h; r++)
        {
            const BYTE* yr = y + (LONG_PTR)r * srcPitch;
            const BYTE* uvr = uv + (LONG_PTR)(r / 2) * srcPitch;
            UINT32* out = reinterpret_cast<UINT32*>(dst + (LONG_PTR)r * dstPitch);
            for (int x = 0; x < w; x++)
            {
                int c = yr[x] - 16, d = uvr[x & ~1] - 128, e = uvr[(x & ~1) + 1] - 128;
                int R = Clip((298 * c + 409 * e + 128) >> 8);
                int G = Clip((298 * c - 100 * d - 208 * e + 128) >> 8);
                int B = Clip((298 * c + 516 * d + 128) >> 8);
                out[x] = 0xFF000000u | (R << 16) | (G << 8) | B;
            }
        }
    }
}

SharedFrameReader::~SharedFrameReader()
{
    Close();
}

void SharedFrameReader::Close()
{
    if (_view) { UnmapViewOfFile(_view); _view = nullptr; }
    if (_mapping) { CloseHandle(_mapping); _mapping = nullptr; }
    if (_file != INVALID_HANDLE_VALUE) { CloseHandle(_file); _file = INVALID_HANDLE_VALUE; }
}

bool SharedFrameReader::EnsureOpen()
{
    if (_view)
        return true;
    LONGLONG now = MFGetSystemTime();
    if (_lastAttempt && now - _lastAttempt < 10000000LL)   // retry once per second
        return false;
    _lastAttempt = now;

    wchar_t path[MAX_PATH];
    if (!WebbyLensSharedPath(path, MAX_PATH, false))
        return false;
    // read-write if WebbyLens.exe granted it (needed for the "camera in use" signal), else read-only
    _writable = true;
    _file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (_file == INVALID_HANDLE_VALUE)
    {
        _writable = false;
        _file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, 0, nullptr);
    }
    if (_file == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (err != _lastError)
            Log(L"Reader: cannot open %s (error %u)", path, err);
        _lastError = err;
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(_file, &size) || size.QuadPart < (LONGLONG)WEBBYLENS_FILE_BYTES)
    {
        Close();
        return false;
    }
    _mapping = CreateFileMappingW(_file, nullptr, _writable ? PAGE_READWRITE : PAGE_READONLY, 0, 0, nullptr);
    if (_mapping)
        _view = MapViewOfFile(_mapping, _writable ? FILE_MAP_WRITE : FILE_MAP_READ, 0, 0, WEBBYLENS_FILE_BYTES);
    if (!_view)
    {
        Log(L"Reader: mapping failed (error %u)", GetLastError());
        Close();
        return false;
    }
    auto hdr = static_cast<const WebbyLensShared*>(_view);
    if (hdr->magic != WEBBYLENS_SHARED_MAGIC || hdr->version != WEBBYLENS_SHARED_VERSION)
    {
        Log(L"Reader: shared file has wrong format");
        Close();
        return false;
    }
    _lastError = 0;
    Log(L"Reader: connected to %s (%s)", path, _writable ? L"read-write" : L"read-only");
    return true;
}

void SharedFrameReader::MarkConsumerActive()
{
    if (!EnsureOpen() || !_writable)
        return;
    auto hdr = static_cast<WebbyLensShared*>(_view);
    InterlockedExchange64(&hdr->consumerBeat, MFGetSystemTime());
}

FrameState SharedFrameReader::Render(const VideoFormat& fmt, BYTE* scan0, LONG pitch)
{
    if (!EnsureOpen())
        return FrameState::NoSignal;

    auto hdr = static_cast<const WebbyLensShared*>(_view);
    if (hdr->paused)
        return FrameState::Paused;
    LONGLONG beat = hdr->heartbeat;
    if (!beat || MFGetSystemTime() - beat > WEBBYLENS_STALE_100NS)
        return FrameState::NoSignal;
    LONG slot = hdr->latest;
    if (slot < 0 || slot >= WEBBYLENS_SLOT_COUNT)
        return FrameState::NoSignal;

    const WebbyLensSlot& s = hdr->slots[slot];
    LONG64 seq = s.seq;
    MemoryBarrier();
    if (seq & 1)
        return FrameState::NoSignal;
    int sw = (int)s.width, sh = (int)s.height;
    if (sw < 2 || sh < 2 || (size_t)sw * sh * 3 / 2 > WEBBYLENS_SLOT_BYTES)
        return FrameState::NoSignal;
    const BYTE* src = WebbyLensSlotData(_view, slot);

    int dw = (int)fmt.width, dh = (int)fmt.height;
    if (fmt.subtype == MFVideoFormat_NV12)
    {
        ScaleNv12Fit(_ry, _ruv, src, sw, sh, scan0, scan0 + (LONG_PTR)pitch * dh, pitch, dw, dh);
    }
    else
    {
        _tmp.resize((size_t)dw * dh * 3 / 2);
        ScaleNv12Fit(_ry, _ruv, src, sw, sh, _tmp.data(), _tmp.data() + (size_t)dw * dh, dw, dw, dh);
        Nv12ToRgb32(_tmp.data(), _tmp.data() + (size_t)dw * dh, dw, scan0, pitch, dw, dh);
    }

    MemoryBarrier();
    if (s.seq != seq)
        _torn++;   // overwritten while copying (very rare with 3 slots); frame still usable
    return FrameState::Live;
}
