// WebbyLensCtl - creates / removes the "WebbyLens" virtual camera.
//
//   WebbyLensCtl run        camera exists while this program runs (press Enter to remove)
//   WebbyLensCtl install    camera stays registered (also after reboot) until "uninstall"
//   WebbyLensCtl uninstall  removes the permanent camera
//
// The media source DLL must first be registered from an elevated prompt:
//   regsvr32 WebbyLensCamera.dll
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <initguid.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>
#include <wrl/client.h>
#include <cstdio>
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

static bool SourceRegistered(std::wstring* dllPath)
{
    std::wstring key = L"Software\\Classes\\CLSID\\" + ClsidString() + L"\\InprocServer32";
    wchar_t path[MAX_PATH] = {};
    DWORD size = sizeof(path);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, path, &size) != ERROR_SUCCESS)
        return false;
    *dllPath = path;
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static HRESULT CreateCamera(MFVirtualCameraLifetime lifetime, IMFVirtualCamera** cam)
{
    return MFCreateVirtualCamera(
        MFVirtualCameraType_SoftwareCameraSource,
        lifetime,
        MFVirtualCameraAccess_CurrentUser,
        kCameraName,
        ClsidString().c_str(),
        nullptr, 0,
        cam);
}

static void PrintError(const wchar_t* what, HRESULT hr)
{
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, hr, 0, (LPWSTR)&msg, 0, nullptr);
    fwprintf(stderr, L"%s failed: 0x%08X %s\n", what, (unsigned)hr, msg ? msg : L"");
    if (msg) LocalFree(msg);
    if (hr == E_ACCESSDENIED)
        fwprintf(stderr, L"Hint: the DLL folder must be readable by the LOCAL SERVICE account.\n");
}

int wmain(int argc, wchar_t** argv)
{
    std::wstring cmd = argc > 1 ? argv[1] : L"";
    if (cmd != L"run" && cmd != L"install" && cmd != L"uninstall")
    {
        wprintf(L"Usage: WebbyLensCtl run | install | uninstall\n"
                L"  run        camera exists while this program runs\n"
                L"  install    camera stays registered until 'uninstall'\n"
                L"  uninstall  remove the permanent camera\n");
        return 1;
    }

    std::wstring dll;
    if (!SourceRegistered(&dll))
    {
        fwprintf(stderr, L"The WebbyLens media source is not registered.\n"
                         L"Run register.bat as administrator first.\n");
        return 2;
    }
    wprintf(L"Media source: %s\n", dll.c_str());

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) { PrintError(L"MFStartup", hr); return 3; }

    int rc = 0;
    ComPtr<IMFVirtualCamera> cam;
    if (cmd == L"run")
    {
        hr = CreateCamera(MFVirtualCameraLifetime_Session, &cam);
        if (SUCCEEDED(hr)) hr = cam->Start(nullptr);
        if (FAILED(hr)) { PrintError(L"Starting the virtual camera", hr); rc = 4; }
        else
        {
            wprintf(L"Virtual camera \"%s\" is running.\n"
                    L"Open Teams / Zoom / Camera app and select it.\n"
                    L"Press Enter to remove the camera and exit...\n", kCameraName);
            getchar();
            hr = cam->Remove();
            wprintf(L"Camera removed (0x%08X).\n", (unsigned)hr);
        }
    }
    else if (cmd == L"install")
    {
        hr = CreateCamera(MFVirtualCameraLifetime_System, &cam);
        if (SUCCEEDED(hr)) hr = cam->Start(nullptr);
        if (FAILED(hr)) { PrintError(L"Installing the virtual camera", hr); rc = 4; }
        else wprintf(L"Virtual camera \"%s\" installed. It stays available until 'WebbyLensCtl uninstall'.\n", kCameraName);
    }
    else // uninstall
    {
        hr = CreateCamera(MFVirtualCameraLifetime_System, &cam);
        if (SUCCEEDED(hr)) hr = cam->Remove();
        if (FAILED(hr)) { PrintError(L"Removing the virtual camera", hr); rc = 4; }
        else wprintf(L"Virtual camera \"%s\" removed.\n", kCameraName);
    }

    cam.Reset();
    MFShutdown();
    CoUninitialize();
    return rc;
}
