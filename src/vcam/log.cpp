#include "common.h"
#include <cstdio>
#include <cstdarg>
#include <string>

static wchar_t g_logPath[MAX_PATH] = {};
static wchar_t g_procName[MAX_PATH] = {};

std::wstring ProcessNameFromPid(DWORD pid)
{
    std::wstring result;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h)
    {
        wchar_t path[MAX_PATH];
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, path, &size))
        {
            const wchar_t* name = wcsrchr(path, L'\\');
            result = name ? name + 1 : path;
        }
        CloseHandle(h);
    }
    return result;
}

void LogInit(HMODULE module)
{
    // %ProgramData%\WebbyLens\logs - writable by the camera service (the program
    // folder under Program Files is not)
    (void)module;
    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ProgramData", base, MAX_PATH);
    if (!n || n >= MAX_PATH)
        wcscpy_s(base, L"C:\\ProgramData");
    wchar_t dir[MAX_PATH];
    swprintf_s(dir, L"%s\\WebbyLens", base);
    CreateDirectoryW(dir, nullptr);
    wcscat_s(dir, L"\\logs");
    CreateDirectoryW(dir, nullptr);
    swprintf_s(g_logPath, L"%s\\WebbyLensCamera.log", dir);

    wchar_t exe[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH))
    {
        const wchar_t* name = wcsrchr(exe, L'\\');
        wcscpy_s(g_procName, name ? name + 1 : exe);
    }
}

void Log(const wchar_t* fmt, ...)
{
    wchar_t msg[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(msg, _TRUNCATE, fmt, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[1400];
    int n = swprintf_s(line, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%u %s] %s\r\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        GetCurrentProcessId(), g_procName, msg);
    if (n <= 0)
        return;

    OutputDebugStringW(line);
    if (!g_logPath[0])
        return;

    char utf8[4200];
    int len = WideCharToMultiByte(CP_UTF8, 0, line, n, utf8, sizeof(utf8), nullptr, nullptr);
    if (len <= 0)
        return;

    HANDLE f = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE)
    {
        DWORD written;
        WriteFile(f, utf8, (DWORD)len, &written, nullptr);
        CloseHandle(f);
    }
}
