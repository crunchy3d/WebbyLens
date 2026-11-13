// WebbyLens virtual camera media source - shared declarations.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ole2.h>
#include <cguid.h>     // must come before ks.h (which redefines GUID_NULL)
#include <initguid.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfvirtualcamera.h>
#include <ks.h>
#include <ksproxy.h>
#include <ksmedia.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <string>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include "../common/resample.h"

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ChainInterfaces;

// CLSID of the WebbyLens media source: {C81B9576-150F-42B2-A6B9-ACCB888C265B}
DEFINE_GUID(CLSID_WebbyLensCamera, 0xc81b9576, 0x150f, 0x42b2, 0xa6, 0xb9, 0xac, 0xcb, 0x88, 0x8c, 0x26, 0x5b);

// Undocumented frame server attribute: PID of the app that opened the camera.
DEFINE_GUID(MF_FRAMESERVER_CLIENTCONTEXT_CLIENTPID, 0x5f8d322e, 0x0fe4, 0x43e4, 0x9e, 0x50, 0xd8, 0x3e, 0xcd, 0x9f, 0xc2, 0xb8);

// ---------------------------------------------------------------------------
// Logging (writes to <dll folder>\logs\WebbyLens.log and OutputDebugString)
void LogInit(HMODULE module);
void Log(const wchar_t* fmt, ...);
std::wstring ProcessNameFromPid(DWORD pid);

// ---------------------------------------------------------------------------
// Video format negotiated with the client
struct VideoFormat
{
    GUID subtype = MFVideoFormat_NV12;
    UINT32 width = 1280;
    UINT32 height = 720;
    UINT32 fps = 30;
};

// Draws the test picture / "no signal" picture into a locked buffer (NV12 or RGB32).
class TestPattern
{
public:
    void Render(const VideoFormat& fmt, BYTE* scan0, LONG pitch, UINT64 frame);
    void RenderNoSignal(const VideoFormat& fmt, BYTE* scan0, LONG pitch);
    void RenderPaused(const VideoFormat& fmt, BYTE* scan0, LONG pitch);
};

enum class FrameState { NoSignal = 0, Live = 1, Paused = 2 };

// Reads phone frames published by WebbyLens.exe (see src/common/shared_frame.h).
class SharedFrameReader
{
public:
    ~SharedFrameReader();
    // Draws the newest phone frame if there is one (Live); otherwise draws nothing.
    FrameState Render(const VideoFormat& fmt, BYTE* scan0, LONG pitch);
    void MarkConsumerActive();   // tells WebbyLens.exe that an app receives frames
    UINT64 TornFrames() const { return _torn; }

private:
    bool EnsureOpen();
    void Close();
    HANDLE _file = INVALID_HANDLE_VALUE;
    HANDLE _mapping = nullptr;
    void* _view = nullptr;
    bool _writable = false;
    LONGLONG _lastAttempt = 0;
    DWORD _lastError = 0;
    UINT64 _torn = 0;
    std::vector<BYTE> _tmp;
    Resampler _ry, _ruv;
};

// ---------------------------------------------------------------------------
// IMFAttributes delegation helper: forwards every IMFAttributes method to A.
#define DELEGATE_IMFATTRIBUTES(A) \
    STDMETHODIMP GetItem(REFGUID k, PROPVARIANT* v) override { return A->GetItem(k, v); } \
    STDMETHODIMP GetItemType(REFGUID k, MF_ATTRIBUTE_TYPE* t) override { return A->GetItemType(k, t); } \
    STDMETHODIMP CompareItem(REFGUID k, REFPROPVARIANT v, BOOL* r) override { return A->CompareItem(k, v, r); } \
    STDMETHODIMP Compare(IMFAttributes* t, MF_ATTRIBUTES_MATCH_TYPE m, BOOL* r) override { return A->Compare(t, m, r); } \
    STDMETHODIMP GetUINT32(REFGUID k, UINT32* v) override { return A->GetUINT32(k, v); } \
    STDMETHODIMP GetUINT64(REFGUID k, UINT64* v) override { return A->GetUINT64(k, v); } \
    STDMETHODIMP GetDouble(REFGUID k, double* v) override { return A->GetDouble(k, v); } \
    STDMETHODIMP GetGUID(REFGUID k, GUID* v) override { return A->GetGUID(k, v); } \
    STDMETHODIMP GetStringLength(REFGUID k, UINT32* l) override { return A->GetStringLength(k, l); } \
    STDMETHODIMP GetString(REFGUID k, LPWSTR s, UINT32 n, UINT32* l) override { return A->GetString(k, s, n, l); } \
    STDMETHODIMP GetAllocatedString(REFGUID k, LPWSTR* s, UINT32* l) override { return A->GetAllocatedString(k, s, l); } \
    STDMETHODIMP GetBlobSize(REFGUID k, UINT32* n) override { return A->GetBlobSize(k, n); } \
    STDMETHODIMP GetBlob(REFGUID k, UINT8* b, UINT32 n, UINT32* l) override { return A->GetBlob(k, b, n, l); } \
    STDMETHODIMP GetAllocatedBlob(REFGUID k, UINT8** b, UINT32* n) override { return A->GetAllocatedBlob(k, b, n); } \
    STDMETHODIMP GetUnknown(REFGUID k, REFIID r, LPVOID* p) override { return A->GetUnknown(k, r, p); } \
    STDMETHODIMP SetItem(REFGUID k, REFPROPVARIANT v) override { return A->SetItem(k, v); } \
    STDMETHODIMP DeleteItem(REFGUID k) override { return A->DeleteItem(k); } \
    STDMETHODIMP DeleteAllItems() override { return A->DeleteAllItems(); } \
    STDMETHODIMP SetUINT32(REFGUID k, UINT32 v) override { return A->SetUINT32(k, v); } \
    STDMETHODIMP SetUINT64(REFGUID k, UINT64 v) override { return A->SetUINT64(k, v); } \
    STDMETHODIMP SetDouble(REFGUID k, double v) override { return A->SetDouble(k, v); } \
    STDMETHODIMP SetGUID(REFGUID k, REFGUID v) override { return A->SetGUID(k, v); } \
    STDMETHODIMP SetString(REFGUID k, LPCWSTR v) override { return A->SetString(k, v); } \
    STDMETHODIMP SetBlob(REFGUID k, const UINT8* b, UINT32 n) override { return A->SetBlob(k, b, n); } \
    STDMETHODIMP SetUnknown(REFGUID k, IUnknown* p) override { return A->SetUnknown(k, p); } \
    STDMETHODIMP LockStore() override { return A->LockStore(); } \
    STDMETHODIMP UnlockStore() override { return A->UnlockStore(); } \
    STDMETHODIMP GetCount(UINT32* n) override { return A->GetCount(n); } \
    STDMETHODIMP GetItemByIndex(UINT32 i, GUID* k, PROPVARIANT* v) override { return A->GetItemByIndex(i, k, v); } \
    STDMETHODIMP CopyAllItems(IMFAttributes* d) override { return A->CopyAllItems(d); }

// ---------------------------------------------------------------------------
// Media stream: one video stream, frames paced by a worker thread.
class MediaStream : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
    ChainInterfaces<IMFMediaStream2, IMFMediaStream, IMFMediaEventGenerator>,
    IMFAttributes, IKsControl>
{
public:
    MediaStream() = default;
    ~MediaStream() override;
    HRESULT RuntimeClassInitialize(IMFMediaSource* source, DWORD streamId);

    // IMFMediaEventGenerator
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* cb, IUnknown* state) override;
    STDMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** ev) override;
    STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** ev) override;
    STDMETHODIMP QueueEvent(MediaEventType met, REFGUID ext, HRESULT status, const PROPVARIANT* value) override;
    // IMFMediaStream
    STDMETHODIMP GetMediaSource(IMFMediaSource** source) override;
    STDMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** desc) override;
    STDMETHODIMP RequestSample(IUnknown* token) override;
    // IMFMediaStream2
    STDMETHODIMP SetStreamState(MF_STREAM_STATE value) override;
    STDMETHODIMP GetStreamState(MF_STREAM_STATE* value) override;
    // IKsControl
    STDMETHODIMP KsProperty(PKSPROPERTY p, ULONG pl, LPVOID d, ULONG dl, ULONG* ret) override;
    STDMETHODIMP KsMethod(PKSMETHOD m, ULONG ml, LPVOID d, ULONG dl, ULONG* ret) override;
    STDMETHODIMP KsEvent(PKSEVENT e, ULONG el, LPVOID d, ULONG dl, ULONG* ret) override;
    // IMFAttributes
    DELEGATE_IMFATTRIBUTES(_attrs)

    // Called by the media source
    HRESULT Start(IMFMediaType* type);
    HRESULT Stop();
    void Shutdown();
    HRESULT SetAllocator(IUnknown* allocator);

private:
    void Worker();
    HRESULT DeliverFrameLocked(IUnknown* token, LONGLONG now, bool* retry);
    HRESULT CreateOwnSample(IMFSample** sample);
    HRESULT FillSample(IMFSample* sample);
    void Draw(BYTE* scan0, LONG pitch);

    std::mutex _lock;
    ComPtr<IMFAttributes> _attrs;
    ComPtr<IMFMediaEventQueue> _queue;
    ComPtr<IMFStreamDescriptor> _desc;
    ComPtr<IMFMediaSource> _source;
    ComPtr<IMFVideoSampleAllocatorEx> _allocator;
    ComPtr<IMFMediaType> _type;
    VideoFormat _fmt;
    MF_STREAM_STATE _state = MF_STREAM_STATE_STOPPED;
    std::deque<ComPtr<IUnknown>> _tokens;
    TestPattern _pattern;
    SharedFrameReader _reader;
    int _live = -1;             // -1 unknown, 0 no signal, 1 phone frames

    std::thread _thread;
    HANDLE _wake = nullptr;
    HANDLE _timer = nullptr;
    bool _exit = false;
    LONGLONG _due = 0;          // earliest time for the next frame (100 ns units)
    UINT64 _frames = 0;
    LONGLONG _statsStart = 0;
    UINT64 _statsFrames = 0;
};

// ---------------------------------------------------------------------------
// Media source exposed to the Windows Frame Server.
class MediaSource : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
    ChainInterfaces<IMFMediaSourceEx, IMFMediaSource, IMFMediaEventGenerator>,
    IMFAttributes, IMFGetService, IKsControl, IMFSampleAllocatorControl>
{
public:
    HRESULT RuntimeClassInitialize(IMFAttributes* activatorAttributes);

    // IMFMediaEventGenerator
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* cb, IUnknown* state) override;
    STDMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** ev) override;
    STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** ev) override;
    STDMETHODIMP QueueEvent(MediaEventType met, REFGUID ext, HRESULT status, const PROPVARIANT* value) override;
    // IMFMediaSource
    STDMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** pd) override;
    STDMETHODIMP GetCharacteristics(DWORD* chars) override;
    STDMETHODIMP Pause() override;
    STDMETHODIMP Shutdown() override;
    STDMETHODIMP Start(IMFPresentationDescriptor* pd, const GUID* timeFormat, const PROPVARIANT* startPos) override;
    STDMETHODIMP Stop() override;
    // IMFMediaSourceEx
    STDMETHODIMP GetSourceAttributes(IMFAttributes** attrs) override;
    STDMETHODIMP GetStreamAttributes(DWORD streamId, IMFAttributes** attrs) override;
    STDMETHODIMP SetD3DManager(IUnknown* manager) override;
    // IMFGetService
    STDMETHODIMP GetService(REFGUID service, REFIID riid, LPVOID* obj) override;
    // IMFSampleAllocatorControl
    STDMETHODIMP SetDefaultAllocator(DWORD streamId, IUnknown* allocator) override;
    STDMETHODIMP GetAllocatorUsage(DWORD streamId, DWORD* inputStreamId, MFSampleAllocatorUsage* usage) override;
    // IKsControl
    STDMETHODIMP KsProperty(PKSPROPERTY p, ULONG pl, LPVOID d, ULONG dl, ULONG* ret) override;
    STDMETHODIMP KsMethod(PKSMETHOD m, ULONG ml, LPVOID d, ULONG dl, ULONG* ret) override;
    STDMETHODIMP KsEvent(PKSEVENT e, ULONG el, LPVOID d, ULONG dl, ULONG* ret) override;
    // IMFAttributes
    DELEGATE_IMFATTRIBUTES(_attrs)

private:
    std::mutex _lock;
    bool _shutdown = false;
    bool _streamSelected = false;
    ComPtr<IMFAttributes> _attrs;
    ComPtr<IMFMediaEventQueue> _queue;
    ComPtr<IMFPresentationDescriptor> _pd;
    ComPtr<MediaStream> _stream;
};

// ---------------------------------------------------------------------------
// Activator: the COM object the Frame Server creates from our CLSID.
class Activator : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
    ChainInterfaces<IMFActivate, IMFAttributes>>
{
public:
    HRESULT RuntimeClassInitialize();

    // IMFActivate
    STDMETHODIMP ActivateObject(REFIID riid, void** ppv) override;
    STDMETHODIMP ShutdownObject() override;
    STDMETHODIMP DetachObject() override;
    // IMFAttributes
    DELEGATE_IMFATTRIBUTES(_attrs)

private:
    ComPtr<IMFAttributes> _attrs;
    ComPtr<MediaSource> _source;
};
