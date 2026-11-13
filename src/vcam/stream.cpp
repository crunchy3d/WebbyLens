#include "common.h"

namespace
{
    struct FormatDef { const GUID* subtype; UINT32 w, h; };

    // Offered formats. The first one is the default.
    // Only HD sizes: when low resolutions are offered, Teams picks 640x360 and then
    // upscales it itself, which looks blocky/combed. Like OBS, we let the app scale down.
    const FormatDef kFormats[] = {
        { &MFVideoFormat_NV12, 1920, 1080 },
        { &MFVideoFormat_NV12, 1280, 720 },
        { &MFVideoFormat_RGB32, 1280, 720 },
    };
    const UINT32 kFps = 30;

    HRESULT MakeVideoType(const GUID& subtype, UINT32 w, UINT32 h, UINT32 fps, IMFMediaType** out)
    {
        ComPtr<IMFMediaType> t;
        HRESULT hr = MFCreateMediaType(&t);
        if (FAILED(hr)) return hr;

        bool nv12 = subtype == MFVideoFormat_NV12;
        UINT32 stride = nv12 ? w : w * 4;
        UINT32 size = nv12 ? w * h * 3 / 2 : w * h * 4;

        t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        t->SetGUID(MF_MT_SUBTYPE, subtype);
        MFSetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, w, h);
        MFSetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, fps, 1);
        MFSetAttributeRatio(t.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        t->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        t->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
        t->SetUINT32(MF_MT_DEFAULT_STRIDE, stride);
        t->SetUINT32(MF_MT_SAMPLE_SIZE, size);
        t->SetUINT32(MF_MT_AVG_BITRATE, size * 8 * fps);
        *out = t.Detach();
        return S_OK;
    }

    const wchar_t* SubtypeName(const GUID& g)
    {
        if (g == MFVideoFormat_NV12) return L"NV12";
        if (g == MFVideoFormat_RGB32) return L"RGB32";
        return L"?";
    }
}

MediaStream::~MediaStream()
{
    {
        std::lock_guard<std::mutex> lk(_lock);
        _exit = true;
    }
    if (_wake) SetEvent(_wake);
    if (_thread.joinable())
    {
        if (_thread.get_id() == std::this_thread::get_id())
            _thread.detach();
        else
            _thread.join();
    }
    if (_wake) CloseHandle(_wake);
    if (_timer) CloseHandle(_timer);
}

HRESULT MediaStream::RuntimeClassInitialize(IMFMediaSource* source, DWORD streamId)
{
    _source = source;

    HRESULT hr = MFCreateAttributes(&_attrs, 8);
    if (FAILED(hr)) return hr;
    _attrs->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
    _attrs->SetUINT32(MF_DEVICESTREAM_STREAM_ID, streamId);
    _attrs->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
    _attrs->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color);

    hr = MFCreateEventQueue(&_queue);
    if (FAILED(hr)) return hr;

    const UINT32 count = ARRAYSIZE(kFormats);
    IMFMediaType* types[count] = {};
    for (UINT32 i = 0; i < count && SUCCEEDED(hr); i++)
        hr = MakeVideoType(*kFormats[i].subtype, kFormats[i].w, kFormats[i].h, kFps, &types[i]);
    if (SUCCEEDED(hr))
        hr = MFCreateStreamDescriptor(streamId, count, types, &_desc);
    if (SUCCEEDED(hr))
    {
        ComPtr<IMFMediaTypeHandler> handler;
        hr = _desc->GetMediaTypeHandler(&handler);
        if (SUCCEEDED(hr))
            hr = handler->SetCurrentMediaType(types[0]);
    }
    for (auto t : types)
        if (t) t->Release();
    if (FAILED(hr)) return hr;

    _wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    _timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!_timer)
        _timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (!_wake || !_timer)
        return HRESULT_FROM_WIN32(GetLastError());

    _thread = std::thread(&MediaStream::Worker, this);
    return S_OK;
}

HRESULT MediaStream::Start(IMFMediaType* type)
{
    std::lock_guard<std::mutex> lk(_lock);
    if (!_queue) return MF_E_SHUTDOWN;

    if (type)
        _type = type;
    if (!_type)
        return MF_E_INVALIDMEDIATYPE;

    GUID subtype = GUID_NULL;
    UINT32 w = 0, h = 0, num = 0, den = 0;
    _type->GetGUID(MF_MT_SUBTYPE, &subtype);
    MFGetAttributeSize(_type.Get(), MF_MT_FRAME_SIZE, &w, &h);
    MFGetAttributeRatio(_type.Get(), MF_MT_FRAME_RATE, &num, &den);
    if ((subtype != MFVideoFormat_NV12 && subtype != MFVideoFormat_RGB32) || !w || !h)
        return MF_E_INVALIDMEDIATYPE;
    _fmt.subtype = subtype;
    _fmt.width = w;
    _fmt.height = h;
    _fmt.fps = (num && den) ? std::max<UINT32>(1, num / den) : kFps;

    HRESULT hr = S_OK;
    if (_allocator)
    {
        _allocator->UninitializeSampleAllocator();
        hr = _allocator->InitializeSampleAllocator(10, _type.Get());
        if (FAILED(hr))
        {
            Log(L"Stream: InitializeSampleAllocator failed 0x%08X, using own samples", hr);
            _allocator.Reset();
        }
    }

    _state = MF_STREAM_STATE_RUNNING;
    _due = MFGetSystemTime();
    _statsStart = _due;
    _statsFrames = 0;
    Log(L"Stream: START %s %ux%u @%u fps, allocator=%s", SubtypeName(subtype), w, h, _fmt.fps,
        _allocator ? L"frame server" : L"own");

    hr = _queue->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, nullptr);
    SetEvent(_wake);
    return hr;
}

HRESULT MediaStream::Stop()
{
    std::lock_guard<std::mutex> lk(_lock);
    if (!_queue) return MF_E_SHUTDOWN;

    _state = MF_STREAM_STATE_STOPPED;
    _tokens.clear();
    if (_allocator)
        _allocator->UninitializeSampleAllocator();
    Log(L"Stream: STOP after %llu frames", _frames);
    return _queue->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr);
}

void MediaStream::Shutdown()
{
    {
        std::lock_guard<std::mutex> lk(_lock);
        _exit = true;
        _state = MF_STREAM_STATE_STOPPED;
        _tokens.clear();
        if (_queue)
        {
            _queue->Shutdown();
            _queue.Reset();
        }
    }
    SetEvent(_wake);
    if (_thread.joinable() && _thread.get_id() != std::this_thread::get_id())
        _thread.join();

    std::lock_guard<std::mutex> lk(_lock);
    if (_allocator)
    {
        _allocator->UninitializeSampleAllocator();
        _allocator.Reset();
    }
    _source.Reset();
    _desc.Reset();
}

HRESULT MediaStream::SetAllocator(IUnknown* allocator)
{
    if (!allocator) return E_POINTER;
    std::lock_guard<std::mutex> lk(_lock);
    _allocator.Reset();
    return allocator->QueryInterface(IID_PPV_ARGS(&_allocator));
}

// ---------------------------------------------------------------------------
// Frame pacing worker: delivers one frame per pending RequestSample,
// never faster than the negotiated frame rate.
void MediaStream::Worker()
{
    for (;;)
    {
        LONGLONG wait = -1; // -1 = wait for a request
        {
            std::lock_guard<std::mutex> lk(_lock);
            if (_exit)
                return;

            if (_state == MF_STREAM_STATE_RUNNING && !_tokens.empty())
            {
                const LONGLONG period = 10000000LL / _fmt.fps;
                LONGLONG now = MFGetSystemTime();
                if (now >= _due)
                {
                    ComPtr<IUnknown> token = _tokens.front();
                    _tokens.pop_front();
                    bool retry = false;
                    HRESULT hr = DeliverFrameLocked(token.Get(), now, &retry);
                    if (retry)
                    {
                        _tokens.push_front(token);
                        _due = now + 20000; // try again in 2 ms
                    }
                    else
                    {
                        if (FAILED(hr))
                            Log(L"Stream: frame delivery failed 0x%08X", hr);
                        _due += period;
                        if (_due < now)
                            _due = now + period; // we were idle: restart cadence
                    }
                    continue;
                }
                wait = _due - now;
            }
        }

        if (wait > 0)
        {
            LARGE_INTEGER due;
            due.QuadPart = -wait; // relative, 100 ns units
            SetWaitableTimer(_timer, &due, 0, nullptr, nullptr, FALSE);
            HANDLE handles[2] = { _wake, _timer };
            WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        }
        else
        {
            WaitForSingleObject(_wake, INFINITE);
        }
    }
}

HRESULT MediaStream::CreateOwnSample(IMFSample** sample)
{
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreate2DMediaBuffer(_fmt.width, _fmt.height, _fmt.subtype.Data1, FALSE, &buffer);
    if (FAILED(hr)) return hr;
    ComPtr<IMFSample> s;
    hr = MFCreateSample(&s);
    if (FAILED(hr)) return hr;
    hr = s->AddBuffer(buffer.Get());
    if (FAILED(hr)) return hr;
    *sample = s.Detach();
    return S_OK;
}

void MediaStream::Draw(BYTE* scan0, LONG pitch)
{
    _reader.MarkConsumerActive();   // an app is pulling frames right now (auto standby)
    FrameState state = _reader.Render(_fmt, scan0, pitch);
    if (state == FrameState::NoSignal)
        _pattern.RenderNoSignal(_fmt, scan0, pitch);
    else if (state == FrameState::Paused)
        _pattern.RenderPaused(_fmt, scan0, pitch);
    if ((int)state != _live)
    {
        Log(state == FrameState::Live ? L"Stream: showing phone video"
            : state == FrameState::Paused ? L"Stream: paused" : L"Stream: no phone video, showing 'no signal'");
        _live = (int)state;
    }
}

HRESULT MediaStream::FillSample(IMFSample* sample)
{
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = sample->GetBufferByIndex(0, &buffer);
    if (FAILED(hr)) return hr;

    const bool nv12 = _fmt.subtype == MFVideoFormat_NV12;
    const DWORD rowBytes = nv12 ? _fmt.width : _fmt.width * 4;
    const DWORD rows = nv12 ? _fmt.height * 3 / 2 : _fmt.height;

    ComPtr<IMF2DBuffer2> buf2;
    ComPtr<IMF2DBuffer> buf1;
    if (SUCCEEDED(buffer.As(&buf2)))
    {
        BYTE* scan0 = nullptr; LONG pitch = 0; BYTE* start = nullptr; DWORD len = 0;
        hr = buf2->Lock2DSize(MF2DBuffer_LockFlags_Write, &scan0, &pitch, &start, &len);
        if (FAILED(hr)) return hr;
        Draw(scan0, pitch);
        buf2->Unlock2D();
        buffer->SetCurrentLength(std::min<DWORD>(len, (DWORD)std::abs(pitch) * rows));
    }
    else if (SUCCEEDED(buffer.As(&buf1)))
    {
        BYTE* scan0 = nullptr; LONG pitch = 0;
        hr = buf1->Lock2D(&scan0, &pitch);
        if (FAILED(hr)) return hr;
        Draw(scan0, pitch);
        buf1->Unlock2D();
        DWORD contiguous = 0;
        if (SUCCEEDED(buf1->GetContiguousLength(&contiguous)))
            buffer->SetCurrentLength(contiguous);
    }
    else
    {
        BYTE* data = nullptr; DWORD maxLen = 0;
        hr = buffer->Lock(&data, &maxLen, nullptr);
        if (FAILED(hr)) return hr;
        if (maxLen < rowBytes * rows)
        {
            buffer->Unlock();
            return MF_E_BUFFERTOOSMALL;
        }
        Draw(data, (LONG)rowBytes);
        buffer->Unlock();
        buffer->SetCurrentLength(rowBytes * rows);
    }
    return S_OK;
}

HRESULT MediaStream::DeliverFrameLocked(IUnknown* token, LONGLONG now, bool* retry)
{
    *retry = false;
    if (!_queue) return MF_E_SHUTDOWN;

    ComPtr<IMFSample> sample;
    HRESULT hr;
    if (_allocator)
    {
        hr = _allocator->AllocateSample(&sample);
        if (hr == MF_E_SAMPLEALLOCATOR_EMPTY)
        {
            *retry = true;
            return hr;
        }
    }
    else
    {
        hr = CreateOwnSample(&sample);
    }
    if (FAILED(hr)) return hr;

    hr = FillSample(sample.Get());
    if (FAILED(hr)) return hr;

    sample->SetSampleTime(now);
    sample->SetSampleDuration(10000000LL / _fmt.fps);
    if (token)
        sample->SetUnknown(MFSampleExtension_Token, token);

    hr = _queue->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.Get());
    if (FAILED(hr)) return hr;

    _frames++;
    _statsFrames++;
    if (_statsFrames == 300)
    {
        double secs = (now - _statsStart) / 1e7;
        Log(L"Stream: %llu frames delivered, %.1f fps", _frames, secs > 0 ? _statsFrames / secs : 0.0);
        _statsStart = now;
        _statsFrames = 0;
    }
    return S_OK;
}

// ---------------------------------------------------------------------------
// IMFMediaEventGenerator (queue calls are made outside our lock: GetEvent can block)
STDMETHODIMP MediaStream::BeginGetEvent(IMFAsyncCallback* cb, IUnknown* state)
{
    ComPtr<IMFMediaEventQueue> q;
    { std::lock_guard<std::mutex> lk(_lock); q = _queue; }
    return q ? q->BeginGetEvent(cb, state) : MF_E_SHUTDOWN;
}

STDMETHODIMP MediaStream::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** ev)
{
    ComPtr<IMFMediaEventQueue> q;
    { std::lock_guard<std::mutex> lk(_lock); q = _queue; }
    return q ? q->EndGetEvent(result, ev) : MF_E_SHUTDOWN;
}

STDMETHODIMP MediaStream::GetEvent(DWORD flags, IMFMediaEvent** ev)
{
    ComPtr<IMFMediaEventQueue> q;
    { std::lock_guard<std::mutex> lk(_lock); q = _queue; }
    return q ? q->GetEvent(flags, ev) : MF_E_SHUTDOWN;
}

STDMETHODIMP MediaStream::QueueEvent(MediaEventType met, REFGUID ext, HRESULT status, const PROPVARIANT* value)
{
    ComPtr<IMFMediaEventQueue> q;
    { std::lock_guard<std::mutex> lk(_lock); q = _queue; }
    return q ? q->QueueEventParamVar(met, ext, status, value) : MF_E_SHUTDOWN;
}

// IMFMediaStream
STDMETHODIMP MediaStream::GetMediaSource(IMFMediaSource** source)
{
    if (!source) return E_POINTER;
    std::lock_guard<std::mutex> lk(_lock);
    if (!_source) return MF_E_SHUTDOWN;
    return _source.CopyTo(source);
}

STDMETHODIMP MediaStream::GetStreamDescriptor(IMFStreamDescriptor** desc)
{
    if (!desc) return E_POINTER;
    std::lock_guard<std::mutex> lk(_lock);
    if (!_desc) return MF_E_SHUTDOWN;
    return _desc.CopyTo(desc);
}

STDMETHODIMP MediaStream::RequestSample(IUnknown* token)
{
    {
        std::lock_guard<std::mutex> lk(_lock);
        if (!_queue) return MF_E_SHUTDOWN;
        if (_state != MF_STREAM_STATE_RUNNING) return MF_E_MEDIA_SOURCE_WRONGSTATE;
        _tokens.emplace_back(token);
    }
    SetEvent(_wake);
    return S_OK;
}

// IMFMediaStream2
STDMETHODIMP MediaStream::SetStreamState(MF_STREAM_STATE value)
{
    MF_STREAM_STATE current;
    {
        std::lock_guard<std::mutex> lk(_lock);
        current = _state;
    }
    Log(L"Stream: SetStreamState %d -> %d", current, value);
    if (current == value)
        return S_OK;

    switch (value)
    {
    case MF_STREAM_STATE_PAUSED:
    {
        if (current != MF_STREAM_STATE_RUNNING)
            return MF_E_INVALID_STATE_TRANSITION;
        std::lock_guard<std::mutex> lk(_lock);
        _state = MF_STREAM_STATE_PAUSED;
        return S_OK;
    }
    case MF_STREAM_STATE_RUNNING:
        return Start(nullptr);
    case MF_STREAM_STATE_STOPPED:
        return Stop();
    default:
        return MF_E_INVALID_STATE_TRANSITION;
    }
}

STDMETHODIMP MediaStream::GetStreamState(MF_STREAM_STATE* value)
{
    if (!value) return E_POINTER;
    std::lock_guard<std::mutex> lk(_lock);
    *value = _state;
    return S_OK;
}

// IKsControl - no camera controls exposed
STDMETHODIMP MediaStream::KsProperty(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG*) { return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND); }
STDMETHODIMP MediaStream::KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG*) { return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND); }
STDMETHODIMP MediaStream::KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG*) { return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND); }
