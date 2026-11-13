#include "common.h"
#include <string>

static HMODULE g_module = nullptr;

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
        LogInit(module);
        Log(L"DLL loaded");
    }
    return TRUE;
}

class ClassFactory : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IClassFactory>
{
public:
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;

        ComPtr<Activator> activator;
        HRESULT hr = Microsoft::WRL::MakeAndInitialize<Activator>(&activator);
        if (FAILED(hr))
        {
            Log(L"ClassFactory: activator init failed 0x%08X", hr);
            return hr;
        }
        return activator.CopyTo(riid, ppv);
    }

    STDMETHODIMP LockServer(BOOL) override { return S_OK; }
};

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (clsid != CLSID_WebbyLensCamera)
        return CLASS_E_CLASSNOTAVAILABLE;

    auto factory = Microsoft::WRL::Make<ClassFactory>();
    if (!factory) return E_OUTOFMEMORY;
    return factory.CopyTo(riid, ppv);
}

STDAPI DllCanUnloadNow()
{
    // Keep the DLL loaded: worker threads may still be winding down.
    return S_FALSE;
}

static std::wstring ClsidString()
{
    wchar_t buf[64];
    StringFromGUID2(CLSID_WebbyLensCamera, buf, ARRAYSIZE(buf));
    return buf;
}

// The Frame Server services only see HKEY_LOCAL_MACHINE, so registration
// must be done from an elevated prompt (regsvr32).
STDAPI DllRegisterServer()
{
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(g_module, path, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());

    std::wstring key = L"Software\\Classes\\CLSID\\" + ClsidString();
    HKEY hk = nullptr;
    LSTATUS st = RegCreateKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hk, nullptr);
    if (st != ERROR_SUCCESS) return HRESULT_FROM_WIN32(st);
    const wchar_t name[] = L"WebbyLens Source";
    RegSetValueExW(hk, nullptr, 0, REG_SZ, (const BYTE*)name, sizeof(name));
    RegCloseKey(hk);

    key += L"\\InprocServer32";
    st = RegCreateKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hk, nullptr);
    if (st != ERROR_SUCCESS) return HRESULT_FROM_WIN32(st);
    st = RegSetValueExW(hk, nullptr, 0, REG_SZ, (const BYTE*)path, (DWORD)((wcslen(path) + 1) * sizeof(wchar_t)));
    const wchar_t model[] = L"Both";
    if (st == ERROR_SUCCESS)
        st = RegSetValueExW(hk, L"ThreadingModel", 0, REG_SZ, (const BYTE*)model, sizeof(model));
    RegCloseKey(hk);
    Log(L"DllRegisterServer: %s", path);
    return HRESULT_FROM_WIN32(st);
}

STDAPI DllUnregisterServer()
{
    std::wstring key = L"Software\\Classes\\CLSID\\" + ClsidString();
    LSTATUS st = RegDeleteTreeW(HKEY_LOCAL_MACHINE, key.c_str());
    Log(L"DllUnregisterServer: %ld", st);
    return (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND) ? S_OK : HRESULT_FROM_WIN32(st);
}
