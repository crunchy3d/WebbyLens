#include "h264_decoder.h"
#include "log.h"
#include <mfidl.h>
#include <mferror.h>
#include <wmcodecdsp.h>
#include <codecapi.h>
#include <strmif.h>

using Microsoft::WRL::ComPtr;

bool H264Decoder::Init(FrameCallback cb)
{
    _cb = std::move(cb);
    HRESULT hr = CoCreateInstance(CLSID_MSH264DecoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&_mft));
    if (FAILED(hr))
    {
        LogLine("H264 decoder: CoCreateInstance failed " + HrStr(hr));
        return false;
    }

    // Low latency: output each frame as soon as it is decoded (no reordering buffer).
    ComPtr<IMFAttributes> attrs;
    if (SUCCEEDED(_mft->GetAttributes(&attrs)))
        attrs->SetUINT32(MF_LOW_LATENCY, TRUE);
    ComPtr<ICodecAPI> codecApi;
    if (SUCCEEDED(_mft.As(&codecApi)))
    {
        VARIANT v; VariantInit(&v);
        v.vt = VT_UI4; v.ulVal = 1;
        codecApi->SetValue(&CODECAPI_AVLowLatencyMode, &v);
    }

    ComPtr<IMFMediaType> in;
    MFCreateMediaType(&in);
    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    hr = _mft->SetInputType(0, in.Get(), 0);
    if (FAILED(hr))
    {
        LogLine("H264 decoder: SetInputType failed " + HrStr(hr));
        return false;
    }
    if (!SetOutputType())
        return false;

    _mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    _mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    return true;
}

bool H264Decoder::SetOutputType()
{
    for (DWORD i = 0;; i++)
    {
        ComPtr<IMFMediaType> t;
        HRESULT hr = _mft->GetOutputAvailableType(0, i, &t);
        if (FAILED(hr))
        {
            LogLine("H264 decoder: no NV12 output type " + HrStr(hr));
            return false;
        }
        GUID sub;
        if (SUCCEEDED(t->GetGUID(MF_MT_SUBTYPE, &sub)) && sub == MFVideoFormat_NV12)
        {
            hr = _mft->SetOutputType(0, t.Get(), 0);
            if (FAILED(hr))
            {
                LogLine("H264 decoder: SetOutputType failed " + HrStr(hr));
                return false;
            }
            MFGetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, &_width, &_height);
            _visW = _width; _visH = _height;
            MFVideoArea area;
            if (SUCCEEDED(t->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8*)&area, sizeof(area), nullptr)))
            {
                _visW = area.Area.cx;
                _visH = area.Area.cy;
            }
            UINT32 stride = 0;
            _stride = SUCCEEDED(t->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) ? (LONG)stride : (LONG)_width;

            MFT_OUTPUT_STREAM_INFO info{};
            _mft->GetOutputStreamInfo(0, &info);
            _providesSamples = (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
            _outBufSize = info.cbSize;
            if (_width)
                LogLine("H264 decoder: output NV12 " + std::to_string(_width) + "x" + std::to_string(_height) +
                    " visible " + std::to_string(_visW) + "x" + std::to_string(_visH));
            return true;
        }
    }
}

bool H264Decoder::Decode(const BYTE* data, size_t size, INT64 ptsUs)
{
    if (!_mft) return false;

    ComPtr<IMFMediaBuffer> buf;
    if (FAILED(MFCreateMemoryBuffer((DWORD)size, &buf)))
        return false;
    BYTE* p;
    buf->Lock(&p, nullptr, nullptr);
    memcpy(p, data, size);
    buf->Unlock();
    buf->SetCurrentLength((DWORD)size);

    ComPtr<IMFSample> sample;
    MFCreateSample(&sample);
    sample->AddBuffer(buf.Get());
    sample->SetSampleTime(ptsUs * 10);

    HRESULT hr = _mft->ProcessInput(0, sample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING)
    {
        DrainOutput();
        hr = _mft->ProcessInput(0, sample.Get(), 0);
    }
    if (FAILED(hr))
    {
        LogLine("H264 decoder: ProcessInput failed " + HrStr(hr));
        return false;
    }
    return DrainOutput();
}

bool H264Decoder::DrainOutput()
{
    for (;;)
    {
        MFT_OUTPUT_DATA_BUFFER out{};
        ComPtr<IMFSample> own;
        if (!_providesSamples)
        {
            ComPtr<IMFMediaBuffer> buf;
            DWORD size = _outBufSize ? _outBufSize : _width * _height * 3 / 2;
            if (!size) size = 1920 * 1088 * 3 / 2;
            if (FAILED(MFCreateMemoryBuffer(size, &buf)))
                return false;
            MFCreateSample(&own);
            own->AddBuffer(buf.Get());
            out.pSample = own.Get();
        }

        DWORD status = 0;
        HRESULT hr = _mft->ProcessOutput(0, 1, &out, &status);
        if (out.pEvents) out.pEvents->Release();

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
            return true;
        if (hr == MF_E_TRANSFORM_STREAM_CHANGE)
        {
            if (!SetOutputType())
                return false;
            continue;
        }
        if (FAILED(hr))
        {
            LogLine("H264 decoder: ProcessOutput failed " + HrStr(hr));
            return false;
        }

        ComPtr<IMFSample> result;
        if (_providesSamples)
            result.Attach(out.pSample);
        else
            result = own;

        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(result->GetBufferByIndex(0, &buf)))
            continue;

        LONGLONG time = 0;
        result->GetSampleTime(&time);

        ComPtr<IMF2DBuffer> buf2d;
        BYTE* scan0 = nullptr;
        LONG pitch = 0;
        bool locked2d = SUCCEEDED(buf.As(&buf2d)) && SUCCEEDED(buf2d->Lock2D(&scan0, &pitch));
        BYTE* raw = nullptr;
        if (!locked2d)
        {
            if (FAILED(buf->Lock(&raw, nullptr, nullptr)))
                continue;
            scan0 = raw;
            pitch = _stride ? _stride : (LONG)_width;
        }

        if (_cb && scan0 && _visW && _visH)
        {
            Nv12Frame f;
            f.y = scan0;
            f.uv = scan0 + (size_t)pitch * _height;   // UV plane follows the (aligned) Y plane
            f.pitch = pitch;
            f.width = _visW;
            f.height = _visH;
            f.pts = time / 10;
            _cb(f);
        }

        if (locked2d) buf2d->Unlock2D();
        else buf->Unlock();
    }
}

void H264Decoder::Reset()
{
    if (_mft)
    {
        _mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        _mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    }
}

void H264Decoder::Close()
{
    if (_mft)
    {
        _mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        _mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        _mft.Reset();
    }
}
