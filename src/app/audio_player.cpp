#include "audio_player.h"
#include "log.h"
#include "process.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <avrt.h>
#include <wrl/client.h>
#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace
{
    const UINT32 kRate = 48000;
    const UINT32 kChannels = 2;
    const size_t kRingFrames = kRate;            // 1 s capacity
    const size_t kTargetFrames = kRate * 60 / 1000;   // start playing at 60 ms buffered
    const size_t kMaxFrames = kRate * 200 / 1000;     // above 200 ms: drop back to the target

    std::wstring Lower(std::wstring s)
    {
        for (auto& c : s) c = (wchar_t)towlower(c);
        return s;
    }

    template <class F>
    void ForEachRenderDevice(F f)
    {
        ComPtr<IMMDeviceEnumerator> en;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))))
            return;
        ComPtr<IMMDeviceCollection> col;
        if (FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)))
            return;
        UINT n = 0;
        col->GetCount(&n);
        for (UINT i = 0; i < n; i++)
        {
            ComPtr<IMMDevice> dev;
            ComPtr<IPropertyStore> props;
            LPWSTR id = nullptr;
            if (FAILED(col->Item(i, &dev)) || FAILED(dev->OpenPropertyStore(STGM_READ, &props)) || FAILED(dev->GetId(&id)))
                continue;
            PROPVARIANT name;
            PropVariantInit(&name);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR)
                f(std::wstring(id), std::wstring(name.pwszVal));
            PropVariantClear(&name);
            CoTaskMemFree(id);
        }
    }
}

std::vector<std::wstring> AudioPlayer::ListDevices()
{
    std::vector<std::wstring> names;
    ForEachRenderDevice([&](const std::wstring&, const std::wstring& name) { names.push_back(name); });
    return names;
}

bool AudioPlayer::Start(const std::wstring& nameMatch, std::string* deviceName, std::string* error)
{
    Stop();
    _deviceId.clear();
    std::wstring want = Lower(nameMatch);
    std::wstring found;
    ForEachRenderDevice([&](const std::wstring& id, const std::wstring& name) {
        std::wstring l = Lower(name);
        // "CABLE Input" must not match "CABLE In 16ch"
        if (_deviceId.empty() && l.find(want) != std::wstring::npos)
        {
            _deviceId = id;
            found = name;
        }
    });
    if (_deviceId.empty())
    {
        *error = "playback device \"" + WideToUtf8(nameMatch) + "\" not found (is VB-Audio Virtual Cable installed?)";
        return false;
    }
    *deviceName = WideToUtf8(found);

    _ring.assign(kRingFrames * kChannels, 0);
    Clear();
    _stop = false;
    _ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    _thread = std::thread(&AudioPlayer::Run, this);
    WaitForSingleObject(_ready, 5000);
    CloseHandle(_ready);
    _ready = nullptr;
    if (FAILED(_startResult))
    {
        *error = "cannot open \"" + *deviceName + "\": " + HrStr(_startResult);
        Stop();
        return false;
    }
    return true;
}

void AudioPlayer::Stop()
{
    _stop = true;
    if (_thread.joinable())
        _thread.join();
}

void AudioPlayer::Clear()
{
    std::lock_guard<std::mutex> lk(_lock);
    _read = 0;
    _count = 0;
    _primed = false;
}

void AudioPlayer::Push(const BYTE* pcm, size_t bytes)
{
    const INT16* in = reinterpret_cast<const INT16*>(pcm);
    size_t frames = bytes / (kChannels * sizeof(INT16));
    std::lock_guard<std::mutex> lk(_lock);
    if (_ring.empty())
        return;
    for (size_t f = 0; f < frames; f++)
    {
        if (_count == kRingFrames)          // full: drop the oldest frame
        {
            _read = (_read + 1) % kRingFrames;
            _count--;
        }
        size_t w = (_read + _count) % kRingFrames;
        _ring[w * 2] = in[f * 2];
        _ring[w * 2 + 1] = in[f * 2 + 1];
        _count++;
    }
    // Keep latency bounded (phone and PC clocks drift apart slowly).
    if (_count > kMaxFrames)
    {
        size_t drop = _count - kTargetFrames;
        _read = (_read + drop) % kRingFrames;
        _count -= drop;
        _dropped += drop;
    }
}

size_t AudioPlayer::Pull(INT16* out, size_t frames)
{
    std::lock_guard<std::mutex> lk(_lock);
    if (!_primed)
    {
        if (_count < kTargetFrames)
            return 0;
        _primed = true;
    }
    size_t n = std::min(frames, _count);
    for (size_t f = 0; f < n; f++)
    {
        out[f * 2] = _ring[_read * 2];
        out[f * 2 + 1] = _ring[_read * 2 + 1];
        _read = (_read + 1) % kRingFrames;
    }
    _count -= n;
    if (n < frames)
    {
        _primed = false;   // ran dry: re-buffer to the target before playing again
        _underruns++;
    }
    return n;
}

void AudioPlayer::Run()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    ComPtr<IMMDeviceEnumerator> en;
    ComPtr<IMMDevice> dev;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> render;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    UINT32 bufferFrames = 0;

    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = kChannels;
    fmt.nSamplesPerSec = kRate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = kChannels * 2;
    fmt.nAvgBytesPerSec = kRate * fmt.nBlockAlign;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
    if (SUCCEEDED(hr)) hr = en->GetDevice(_deviceId.c_str(), &dev);
    if (SUCCEEDED(hr)) hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
    if (SUCCEEDED(hr))
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            200000 /* 20 ms */, 0, &fmt, nullptr);
    if (SUCCEEDED(hr)) hr = client->SetEventHandle(event);
    if (SUCCEEDED(hr)) hr = client->GetBufferSize(&bufferFrames);
    if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&render));
    if (SUCCEEDED(hr))
    {
        BYTE* data;
        if (SUCCEEDED(render->GetBuffer(bufferFrames, &data)))
            render->ReleaseBuffer(bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
        hr = client->Start();
    }
    _startResult = hr;
    SetEvent(_ready);

    if (SUCCEEDED(hr))
    {
        LogLine("audio: playing into device, buffer " + std::to_string(bufferFrames) + " frames");
        ULONGLONG lastStats = GetTickCount64();
        while (!_stop)
        {
            if (WaitForSingleObject(event, 200) != WAIT_OBJECT_0)
                continue;
            UINT32 padding = 0;
            if (FAILED(client->GetCurrentPadding(&padding)))
                break;
            UINT32 avail = bufferFrames - padding;
            if (!avail)
                continue;
            BYTE* data = nullptr;
            if (FAILED(render->GetBuffer(avail, &data)))
                break;
            INT16* out = reinterpret_cast<INT16*>(data);
            size_t got = Pull(out, avail);
            if (got < avail)
                memset(out + got * kChannels, 0, (avail - got) * kChannels * sizeof(INT16));
            render->ReleaseBuffer(avail, got ? 0 : AUDCLNT_BUFFERFLAGS_SILENT);

            if (GetTickCount64() - lastStats > 30000)
            {
                lastStats = GetTickCount64();
                std::lock_guard<std::mutex> lk(_lock);
                LogLine("audio stats: buffered " + std::to_string(_count * 1000 / kRate) + " ms, underruns " +
                        std::to_string(_underruns) + ", dropped " + std::to_string(_dropped * 1000 / kRate) + " ms");
            }
        }
        client->Stop();
    }
    else
    {
        LogLine("audio: cannot open device " + HrStr(hr));
    }

    render.Reset();
    client.Reset();
    dev.Reset();
    en.Reset();
    CloseHandle(event);
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    CoUninitialize();
}
