#include <windows.h>
#include <initguid.h>
#include "vcam_control.h"
#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

// Must match src/vcam/common.h
DEFINE_GUID(CLSID_WebbyLensCamera, 0xc81b9576, 0x150f, 0x42b2, 0xa6, 0xb9, 0xac, 0xcb, 0x88, 0x8c, 0x26, 0x5b);
static const wchar_t* kCameraName = L"WebbyLens";

static std::wstring ClsidString()
{
    wchar_t buf[64];
    StringFromGUID2(CLSID_WebbyLensCamera, buf, ARRAYSIZE(buf));
    return buf;
}

bool VcamSourceRegistered()
{
    std::wstring key = L"Software\\Classes\\CLSID\\" + ClsidString() + L"\\InprocServer32";
    wchar_t path[MAX_PATH] = {};
    DWORD size = sizeof(path);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, path, &size) != ERROR_SUCCESS)
        return false;
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

bool VcamDeviceExists()
{
    ComPtr<IMFAttributes> attrs;
    if (FAILED(MFCreateAttributes(&attrs, 1)))
        return false;
    attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    if (FAILED(MFEnumDeviceSources(attrs.Get(), &devices, &count)))
        return false;
    bool found = false;
    for (UINT32 i = 0; i < count; i++)
    {
        WCHAR* name = nullptr;
        UINT32 len = 0;
        if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &len)))
        {
            if (lstrcmpiW(name, kCameraName) == 0)
                found = true;
            CoTaskMemFree(name);
        }
        devices[i]->Release();
    }
    CoTaskMemFree(devices);
    return found;
}

HRESULT VcamCreateSession(IMFVirtualCamera** cam)
{
    ComPtr<IMFVirtualCamera> c;
    HRESULT hr = MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource, MFVirtualCameraLifetime_Session,
                                       MFVirtualCameraAccess_CurrentUser, kCameraName, ClsidString().c_str(),
                                       nullptr, 0, &c);
    if (SUCCEEDED(hr))
        hr = c->Start(nullptr);
    if (SUCCEEDED(hr))
        *cam = c.Detach();
    return hr;
}
