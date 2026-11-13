#include "common.h"

HRESULT MediaSource::RuntimeClassInitialize(IMFAttributes* activatorAttributes)
{
    HRESULT hr = MFCreateAttributes(&_attrs, 8);
    if (FAILED(hr)) return hr;
    if (activatorAttributes)
        activatorAttributes->CopyAllItems(_attrs.Get());

    // Sensor profile (Windows Hello / camera apps query this)
    ComPtr<IMFSensorProfileCollection> profiles;
    ComPtr<IMFSensorProfile> profile;
    if (SUCCEEDED(MFCreateSensorProfileCollection(&profiles)) &&
        SUCCEEDED(MFCreateSensorProfile(KSCAMERAPROFILE_Legacy, 0, nullptr, &profile)) &&
        SUCCEEDED(profile->AddProfileFilter(0, L"((RES==;FRT<=30,1;SUT==))")) &&
        SUCCEEDED(profiles->AddProfile(profile.Get())))
    {
        _attrs->SetUnknown(MF_DEVICEMFT_SENSORPROFILE_COLLECTION, profiles.Get());
    }

    hr = Microsoft::WRL::MakeAndInitialize<MediaStream>(&_stream, static_cast<IMFMediaSource*>(this), 0);
    if (FAILED(hr)) return hr;

    ComPtr<IMFStreamDescriptor> desc;
    hr = _stream->GetStreamDescriptor(&desc);
    if (FAILED(hr)) return hr;
    IMFStreamDescriptor* descs[1] = { desc.Get() };
    hr = MFCreatePresentationDescriptor(1, descs, &_pd);
    if (FAILED(hr)) return hr;

    return MFCreateEventQueue(&_queue);
}

// IMFMediaEventGenerator
STDMETHODIMP MediaSource::BeginGetEvent(IMFAsyncCallback* cb, IUnknown* state)
{
    ComPtr<IMFMediaEventQueue> q;
    { std::lock_guard<std::mutex> lk(_lock); q = _queue; }
    return q ? q->BeginGetEvent(cb, state) : MF_E_SHUTDOWN;
}

STDMETHODIMP MediaSource::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** ev)
{
    ComPtr<IMFMediaEventQueue> q;
    { std::lock_guard<std::mutex> lk(_lock); q = _queue; }
    return q ? q->EndGetEvent(result, ev) : MF_E_SHUTDOWN;
}

STDMETHODIMP MediaSource::GetEvent(DWORD flags, IMFMediaEvent** ev)
{
    ComPtr<IMFMediaEventQueue> q;
    { std::lock_guard<std::mutex> lk(_lock); q = _queue; }
    return q ? q->GetEvent(flags, ev) : MF_E_SHUTDOWN;
}

STDMETHODIMP MediaSource::QueueEvent(MediaEventType met, REFGUID ext, HRESULT status, const PROPVARIANT* value)
{
    ComPtr<IMFMediaEventQueue> q;
    { std::lock_guard<std::mutex> lk(_lock); q = _queue; }
    return q ? q->QueueEventParamVar(met, ext, status, value) : MF_E_SHUTDOWN;
}

// IMFMediaSource
STDMETHODIMP MediaSource::CreatePresentationDescriptor(IMFPresentationDescriptor** pd)
{
    if (!pd) return E_POINTER;
    std::lock_guard<std::mutex> lk(_lock);
    if (_shutdown) return MF_E_SHUTDOWN;
    return _pd->Clone(pd);
}

STDMETHODIMP MediaSource::GetCharacteristics(DWORD* chars)
{
    if (!chars) return E_POINTER;
    *chars = MFMEDIASOURCE_IS_LIVE;
    return S_OK;
}

STDMETHODIMP MediaSource::Pause()
{
    return MF_E_INVALID_STATE_TRANSITION;
}

STDMETHODIMP MediaSource::Start(IMFPresentationDescriptor* pd, const GUID* timeFormat, const PROPVARIANT* startPos)
{
    if (!pd || !startPos) return E_POINTER;
    if (timeFormat && *timeFormat != GUID_NULL) return MF_E_UNSUPPORTED_TIME_FORMAT;

    std::lock_guard<std::mutex> lk(_lock);
    if (_shutdown) return MF_E_SHUTDOWN;

    DWORD count = 0;
    HRESULT hr = pd->GetStreamDescriptorCount(&count);
    if (FAILED(hr)) return hr;
    if (count != 1) return E_INVALIDARG;

    BOOL selected = FALSE;
    ComPtr<IMFStreamDescriptor> desc;
    hr = pd->GetStreamDescriptorByIndex(0, &selected, &desc);
    if (FAILED(hr)) return hr;

    if (selected)
    {
        ComPtr<IMFMediaTypeHandler> handler;
        ComPtr<IMFMediaType> type;
        hr = desc->GetMediaTypeHandler(&handler);
        if (SUCCEEDED(hr))
            hr = handler->GetCurrentMediaType(&type);
        if (FAILED(hr)) return hr;

        _pd->SelectStream(0);
        ComPtr<IUnknown> unk;
        _stream.As(&unk);
        hr = _queue->QueueEventParamUnk(_streamSelected ? MEUpdatedStream : MENewStream, GUID_NULL, S_OK, unk.Get());
        if (FAILED(hr)) return hr;
        _streamSelected = true;

        hr = _stream->Start(type.Get());
        if (FAILED(hr))
        {
            Log(L"Source: stream start failed 0x%08X", hr);
            return hr;
        }
    }
    else if (_streamSelected)
    {
        _pd->DeselectStream(0);
        _stream->Stop();
        _streamSelected = false;
    }

    PROPVARIANT time;
    InitPropVariantFromInt64(MFGetSystemTime(), &time);
    hr = _queue->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, &time);
    PropVariantClear(&time);
    return hr;
}

STDMETHODIMP MediaSource::Stop()
{
    std::lock_guard<std::mutex> lk(_lock);
    if (_shutdown) return MF_E_SHUTDOWN;

    if (_streamSelected)
    {
        _stream->Stop();
        _pd->DeselectStream(0);
        _streamSelected = false;
    }

    PROPVARIANT time;
    InitPropVariantFromInt64(MFGetSystemTime(), &time);
    HRESULT hr = _queue->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, &time);
    PropVariantClear(&time);
    return hr;
}

STDMETHODIMP MediaSource::Shutdown()
{
    ComPtr<MediaStream> stream;
    {
        std::lock_guard<std::mutex> lk(_lock);
        if (_shutdown) return MF_E_SHUTDOWN;
        _shutdown = true;
        Log(L"Source: Shutdown");
        if (_queue)
            _queue->Shutdown();
        stream = _stream;
    }
    if (stream)
        stream->Shutdown(); // breaks the stream -> source reference cycle
    return S_OK;
}

// IMFMediaSourceEx
STDMETHODIMP MediaSource::GetSourceAttributes(IMFAttributes** attrs)
{
    if (!attrs) return E_POINTER;
    return QueryInterface(IID_PPV_ARGS(attrs));
}

STDMETHODIMP MediaSource::GetStreamAttributes(DWORD streamId, IMFAttributes** attrs)
{
    if (!attrs) return E_POINTER;
    *attrs = nullptr;
    if (streamId != 0) return MF_E_INVALIDSTREAMNUMBER;
    std::lock_guard<std::mutex> lk(_lock);
    if (!_stream) return MF_E_SHUTDOWN;
    return _stream.CopyTo(attrs);
}

STDMETHODIMP MediaSource::SetD3DManager(IUnknown*)
{
    // CPU-only for now: frames are produced in system memory.
    return S_OK;
}

// IMFGetService
STDMETHODIMP MediaSource::GetService(REFGUID, REFIID, LPVOID* obj)
{
    if (obj) *obj = nullptr;
    return MF_E_UNSUPPORTED_SERVICE;
}

// IMFSampleAllocatorControl
STDMETHODIMP MediaSource::SetDefaultAllocator(DWORD streamId, IUnknown* allocator)
{
    if (streamId != 0) return MF_E_INVALIDSTREAMNUMBER;
    std::lock_guard<std::mutex> lk(_lock);
    if (!_stream) return MF_E_SHUTDOWN;
    return _stream->SetAllocator(allocator);
}

STDMETHODIMP MediaSource::GetAllocatorUsage(DWORD streamId, DWORD* inputStreamId, MFSampleAllocatorUsage* usage)
{
    if (!inputStreamId || !usage) return E_POINTER;
    if (streamId != 0) return MF_E_INVALIDSTREAMNUMBER;
    *inputStreamId = streamId;
    *usage = MFSampleAllocatorUsage_UsesProvidedAllocator;
    return S_OK;
}

// IKsControl - no camera controls exposed
STDMETHODIMP MediaSource::KsProperty(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG*) { return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND); }
STDMETHODIMP MediaSource::KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG*) { return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND); }
STDMETHODIMP MediaSource::KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG*) { return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND); }

// ---------------------------------------------------------------------------
// Activator

HRESULT Activator::RuntimeClassInitialize()
{
    HRESULT hr = MFCreateAttributes(&_attrs, 4);
    if (FAILED(hr)) return hr;
    _attrs->SetUINT32(MF_VIRTUALCAMERA_PROVIDE_ASSOCIATED_CAMERA_SOURCES, 1);
    _attrs->SetGUID(MFT_TRANSFORM_CLSID_Attribute, CLSID_WebbyLensCamera);
    return Microsoft::WRL::MakeAndInitialize<MediaSource>(&_source, _attrs.Get());
}

STDMETHODIMP Activator::ActivateObject(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    UINT32 pid = 0;
    if (SUCCEEDED(_attrs->GetUINT32(MF_FRAMESERVER_CLIENTCONTEXT_CLIENTPID, &pid)) && pid)
        Log(L"Activator: opened by client pid %u (%s)", pid, ProcessNameFromPid(pid).c_str());
    else
        Log(L"Activator: ActivateObject");

    if (!_source) return MF_E_SHUTDOWN;
    return _source.CopyTo(riid, ppv);
}

STDMETHODIMP Activator::ShutdownObject()
{
    return S_OK;
}

STDMETHODIMP Activator::DetachObject()
{
    _source.Reset();
    return S_OK;
}
