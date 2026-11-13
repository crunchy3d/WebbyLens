#include "settings.h"
#include "process.h"

static const wchar_t* kKey = L"Software\\WebbyLens";
static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static std::wstring PhoneKey(const std::wstring& serial)
{
    std::wstring safe = serial;   // a registry key name must not contain '\'
    for (auto& c : safe)
        if (c == L'\\' || c == L'/') c = L'_';
    return std::wstring(kKey) + L"\\Phones\\" + safe;
}

static DWORD GetDword(HKEY k, const wchar_t* name, DWORD def)
{
    DWORD v = 0, size = sizeof(v);
    return RegGetValueW(k, nullptr, name, RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS ? v : def;
}

static std::string GetString(HKEY k, const wchar_t* name, const std::string& def)
{
    wchar_t buf[256];
    DWORD size = sizeof(buf);
    if (RegGetValueW(k, nullptr, name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
        return def;
    return WideToUtf8(buf);
}

static std::wstring GetWString(HKEY k, const wchar_t* name)
{
    wchar_t buf[256];
    DWORD size = sizeof(buf);
    return RegGetValueW(k, nullptr, name, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS ? buf : L"";
}

static void SetWString(HKEY k, const wchar_t* name, const std::wstring& w)
{
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)w.c_str(), (DWORD)((w.size() + 1) * sizeof(wchar_t)));
}

// per-phone values (also stored at the top level as "last used", the start values for a new phone)
static void ReadPhoneValues(HKEY k, AppSettings* s)
{
    s->facing = GetString(k, L"Facing", s->facing);
    s->zoom = GetDword(k, L"Zoom100", (DWORD)(s->zoom * 100)) / 100.0f;
    s->maxSize = (int)GetDword(k, L"MaxSize", s->maxSize);
    s->rotation = (int)GetDword(k, L"Rotation", s->rotation);
    s->micSource = GetString(k, L"MicSource", s->micSource);
    if (s->facing != "front" && s->facing != "back") s->facing = "front";
    if (s->zoom < 0.3f || s->zoom > 10.0f) s->zoom = 1.0f;
    if (s->maxSize != 1920 && s->maxSize != 1280) s->maxSize = 1920;
    if (s->rotation % 90 || s->rotation < 0 || s->rotation > 270) s->rotation = 0;
    if (s->micSource != "mic" && s->micSource != "mic-voice-communication") s->micSource = "mic";
}

static void WritePhoneValues(HKEY k, const AppSettings& s);

static void SetDword(HKEY k, const wchar_t* name, DWORD v)
{
    RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
}

static void SetString(HKEY k, const wchar_t* name, const std::string& v)
{
    std::wstring w = Utf8ToWide(v);
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)w.c_str(), (DWORD)((w.size() + 1) * sizeof(wchar_t)));
}

void AppSettings::Load()
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ, &k) != ERROR_SUCCESS)
        return;
    DWORD m = GetDword(k, L"Mode", (DWORD)mode);
    if (m <= 3) mode = (Mode)m;
    ReadPhoneValues(k, this);
    phone = GetWString(k, L"Phone");
    mic = GetDword(k, L"Mic", mic) != 0;
    mirror = GetDword(k, L"Mirror", mirror) != 0;
    autoStandby = GetDword(k, L"AutoStandby", autoStandby) != 0;
    opacity = (int)GetDword(k, L"Opacity", opacity);
    window.left = (LONG)GetDword(k, L"WinLeft", 0);
    window.top = (LONG)GetDword(k, L"WinTop", 0);
    window.right = (LONG)GetDword(k, L"WinRight", 0);
    window.bottom = (LONG)GetDword(k, L"WinBottom", 0);
    RegCloseKey(k);

    LoadPhone(phone);   // the last phone's own values (and name) win over the top-level copy
    if (opacity < 30 || opacity > 100) opacity = 100;
}

void AppSettings::Save() const
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return;
    SetDword(k, L"Mode", (DWORD)mode);
    WritePhoneValues(k, *this);
    SetWString(k, L"Phone", phone);
    SetDword(k, L"Mic", mic);
    SetDword(k, L"Mirror", mirror);
    SetDword(k, L"AutoStandby", autoStandby);
    SetDword(k, L"Opacity", (DWORD)opacity);
    SetDword(k, L"WinLeft", (DWORD)window.left);
    SetDword(k, L"WinTop", (DWORD)window.top);
    SetDword(k, L"WinRight", (DWORD)window.right);
    SetDword(k, L"WinBottom", (DWORD)window.bottom);
    RegCloseKey(k);

    if (!phone.empty() &&
        RegCreateKeyExW(HKEY_CURRENT_USER, PhoneKey(phone).c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS)
    {
        WritePhoneValues(k, *this);
        if (!phoneName.empty()) SetWString(k, L"Name", phoneName);
        RegCloseKey(k);
    }
}

bool AppSettings::LoadPhone(const std::wstring& serial)
{
    HKEY k;
    if (serial.empty() || RegOpenKeyExW(HKEY_CURRENT_USER, PhoneKey(serial).c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS)
        return false;
    ReadPhoneValues(k, this);
    std::wstring name = GetWString(k, L"Name");
    if (!name.empty()) phoneName = name;
    RegCloseKey(k);
    return true;
}

static void WritePhoneValues(HKEY k, const AppSettings& s)
{
    SetString(k, L"Facing", s.facing);
    SetDword(k, L"Zoom100", (DWORD)(s.zoom * 100 + 0.5f));
    SetDword(k, L"MaxSize", (DWORD)s.maxSize);
    SetDword(k, L"Rotation", (DWORD)s.rotation);
    SetString(k, L"MicSource", s.micSource);
}

bool AutostartEnabled()
{
    wchar_t buf[MAX_PATH * 2];
    DWORD size = sizeof(buf);
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, L"WebbyLens", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS;
}

void SetAutostart(bool on)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_WRITE, &k) != ERROR_SUCCESS)
        return;
    if (on)
    {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring v = L"\"" + std::wstring(exe) + L"\"";
        RegSetValueExW(k, L"WebbyLens", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        RegDeleteValueW(k, L"WebbyLens");
    }
    RegCloseKey(k);
}
