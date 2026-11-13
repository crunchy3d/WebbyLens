#include "audio_mute.h"
#include "log.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

static const wchar_t* kMicName = L"CABLE Output";
static const wchar_t* kKey = L"Software\\WebbyLens";

bool SetRecordingDeviceMute(const std::wstring& nameMatch, bool mute)
{
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))))
        return false;
    ComPtr<IMMDeviceCollection> col;
    if (FAILED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &col)))
        return false;
    UINT n = 0;
    col->GetCount(&n);
    bool done = false;
    for (UINT i = 0; i < n && !done; i++)
    {
        ComPtr<IMMDevice> dev;
        ComPtr<IPropertyStore> ps;
        if (FAILED(col->Item(i, &dev)) || FAILED(dev->OpenPropertyStore(STGM_READ, &ps)))
            continue;
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR && wcsstr(v.pwszVal, nameMatch.c_str()))
        {
            ComPtr<IAudioEndpointVolume> vol;
            if (SUCCEEDED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &vol)))
                done = SUCCEEDED(vol->SetMute(mute, nullptr));
        }
        PropVariantClear(&v);
    }
    return done;
}

bool RecordingDeviceInUse(const std::wstring& nameMatch)
{
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))))
        return false;
    ComPtr<IMMDeviceCollection> col;
    if (FAILED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &col)))
        return false;
    UINT n = 0;
    col->GetCount(&n);
    for (UINT i = 0; i < n; i++)
    {
        ComPtr<IMMDevice> dev;
        ComPtr<IPropertyStore> ps;
        if (FAILED(col->Item(i, &dev)) || FAILED(dev->OpenPropertyStore(STGM_READ, &ps)))
            continue;
        PROPVARIANT v;
        PropVariantInit(&v);
        bool match = SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR &&
                     wcsstr(v.pwszVal, nameMatch.c_str());
        PropVariantClear(&v);
        if (!match)
            continue;

        ComPtr<IAudioSessionManager2> mgr;
        ComPtr<IAudioSessionEnumerator> sessions;
        if (FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &mgr)) ||
            FAILED(mgr->GetSessionEnumerator(&sessions)))
            return false;
        int count = 0;
        sessions->GetCount(&count);
        for (int s = 0; s < count; s++)
        {
            ComPtr<IAudioSessionControl> ctl;
            AudioSessionState state;
            if (SUCCEEDED(sessions->GetSession(s, &ctl)) && SUCCEEDED(ctl->GetState(&state)) &&
                state == AudioSessionStateActive)
                return true;
        }
        return false;
    }
    return false;
}

static void SetFlag(bool on)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return;
    if (on)
    {
        DWORD one = 1;
        RegSetValueExW(k, L"MicMutedByPause", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
    }
    else
    {
        RegDeleteValueW(k, L"MicMutedByPause");
    }
    RegCloseKey(k);
}

void PauseMicMute(bool mute)
{
    if (mute) SetFlag(true);
    bool ok = SetRecordingDeviceMute(kMicName, mute);
    LogLine(std::string(mute ? "pause: CABLE Output muted" : "resume: CABLE Output unmuted") + (ok ? "" : " (FAILED)"));
    if (!mute) SetFlag(false);
}

void RestoreMicIfLeftMuted()
{
    DWORD v = 0, size = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, kKey, L"MicMutedByPause", RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS && v)
    {
        LogLine("CABLE Output was left muted by an earlier pause - unmuting");
        PauseMicMute(false);
    }
}
