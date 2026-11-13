#include "log.h"
#include <cstdio>
#include <mutex>

static std::mutex g_lock;
static FILE* g_file = nullptr;
static bool g_echo = false;

std::wstring LogDir()
{
    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    std::wstring dir = (n && n < MAX_PATH) ? std::wstring(base) : std::wstring(L".");
    dir += L"\\WebbyLens";
    CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\logs";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void LogOpen(const std::wstring& path, bool echoToConsole)
{
    std::lock_guard<std::mutex> lk(g_lock);
    g_echo = echoToConsole;
    if (!g_file)
        g_file = _wfsopen(path.c_str(), L"a", _SH_DENYNO);
}

static void Write(const std::string& line, bool console)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    char ts[32];
    sprintf_s(ts, "%02u:%02u:%02u.%03u", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    std::lock_guard<std::mutex> lk(g_lock);
    if (g_file)
    {
        fprintf(g_file, "%04u-%02u-%02u %s %s\n", st.wYear, st.wMonth, st.wDay, ts, line.c_str());
        fflush(g_file);
    }
    if (console)
    {
        printf("%s %s\n", ts, line.c_str());
        fflush(stdout);
    }
}

void LogLine(const std::string& line) { Write(line, g_echo); }
void Status(const std::string& line) { Write(line, true); }

std::string HrStr(HRESULT hr)
{
    char buf[16];
    sprintf_s(buf, "0x%08X", (unsigned)hr);
    return buf;
}
