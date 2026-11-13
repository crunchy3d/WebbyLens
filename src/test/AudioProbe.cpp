// AudioProbe - records from a capture device (default "CABLE Output") for a few
// seconds and prints the level, i.e. what Teams/Zoom would hear.
//   AudioProbe.exe [seconds] [device name part]
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <string>

using Microsoft::WRL::ComPtr;

int wmain(int argc, wchar_t** argv)
{
    int seconds = argc > 1 ? _wtoi(argv[1]) : 5;
    std::wstring want = argc > 2 ? argv[2] : L"CABLE Output";
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ComPtr<IMMDeviceEnumerator> en;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
    ComPtr<IMMDeviceCollection> col;
    en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &col);
    UINT n = 0;
    col->GetCount(&n);
    ComPtr<IMMDevice> dev;
    std::wstring devName;
    for (UINT i = 0; i < n && !dev; i++)
    {
        ComPtr<IMMDevice> d;
        ComPtr<IPropertyStore> ps;
        col->Item(i, &d);
        d->OpenPropertyStore(STGM_READ, &ps);
        PROPVARIANT v; PropVariantInit(&v);
        ps->GetValue(PKEY_Device_FriendlyName, &v);
        if (v.vt == VT_LPWSTR && wcsstr(v.pwszVal, want.c_str()))
        {
            dev = d;
            devName = v.pwszVal;
        }
        PropVariantClear(&v);
    }
    if (!dev) { wprintf(L"Capture device \"%s\" not found\n", want.c_str()); return 1; }

    ComPtr<IAudioClient> client;
    dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
    WAVEFORMATEX fmt{ WAVE_FORMAT_PCM, 2, 48000, 48000 * 4, 4, 16, 0 };
    HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, 1000000, 0, &fmt, nullptr);
    if (FAILED(hr)) { wprintf(L"Initialize failed 0x%08X\n", hr); return 1; }
    ComPtr<IAudioCaptureClient> cap;
    client->GetService(IID_PPV_ARGS(&cap));
    client->Start();
    wprintf(L"Recording from \"%s\" for %d s (level per 0.5 s, dBFS):\n", devName.c_str(), seconds);

    double sum = 0; UINT64 count = 0; int peak = 0;
    ULONGLONG start = GetTickCount64(), last = start;
    while (GetTickCount64() - start < (ULONGLONG)seconds * 1000)
    {
        Sleep(10);
        UINT32 packet = 0;
        while (SUCCEEDED(cap->GetNextPacketSize(&packet)) && packet)
        {
            BYTE* data; UINT32 frames; DWORD flags;
            cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            const INT16* s = reinterpret_cast<const INT16*>(data);
            for (UINT32 i = 0; i < frames * 2; i++)
            {
                int v = (flags & AUDCLNT_BUFFERFLAGS_SILENT) ? 0 : s[i];
                sum += (double)v * v; count++;
                if (abs(v) > peak) peak = abs(v);
            }
            cap->ReleaseBuffer(frames);
        }
        if (GetTickCount64() - last >= 500)
        {
            last = GetTickCount64();
            double rms = count ? sqrt(sum / count) : 0;
            double db = rms > 0 ? 20 * log10(rms / 32768.0) : -120;
            int bars = db < -60 ? 0 : (int)((db + 60) / 2);
            wprintf(L"  %6.1f dB  peak %5d  %.*s\n", db, peak, bars, L"##############################");
            sum = 0; count = 0; peak = 0;
        }
    }
    client->Stop();
    CoUninitialize();
    return 0;
}
