// H.264 (Annex B) -> NV12 using the Media Foundation decoder built into Windows.
#pragma once
#include <windows.h>
#include <mfapi.h>
#include <mftransform.h>
#include <wrl/client.h>
#include <functional>

struct Nv12Frame
{
    const BYTE* y;
    const BYTE* uv;
    LONG pitch;           // same pitch for Y and UV planes
    UINT32 width, height; // visible size
    INT64 pts;            // microseconds
};

class H264Decoder
{
public:
    using FrameCallback = std::function<void(const Nv12Frame&)>;

    bool Init(FrameCallback cb);
    // Feeds one access unit. Frames are delivered synchronously through the callback.
    bool Decode(const BYTE* data, size_t size, INT64 ptsUs);
    void Reset();         // after a reconnect
    void Close();

private:
    bool SetOutputType();
    bool DrainOutput();

    Microsoft::WRL::ComPtr<IMFTransform> _mft;
    FrameCallback _cb;
    UINT32 _width = 0, _height = 0;          // decoded (aligned) size
    UINT32 _visW = 0, _visH = 0;             // visible size (aperture)
    LONG _stride = 0;
    bool _providesSamples = false;
    DWORD _outBufSize = 0;
};
