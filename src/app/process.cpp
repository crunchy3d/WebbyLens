#include "process.h"
#include <vector>

static HANDLE Job()
{
    static HANDLE job = [] {
        HANDLE j = CreateJobObjectW(nullptr, nullptr);
        if (j)
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK;
            SetInformationJobObject(j, JobObjectExtendedLimitInformation, &info, sizeof(info));
        }
        return j;
    }();
    return job;
}

std::wstring ExeDir()
{
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path, n);
    size_t slash = s.find_last_of(L'\\');
    return slash == std::wstring::npos ? L"." : s.substr(0, slash);
}

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string WideToUtf8(const std::wstring& s)
{
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string a(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &a[0], n, nullptr, nullptr);
    return a;
}

// Creates a child with stdout/stderr redirected to a pipe; returns the read end.
// detached = not placed in our job (used for "adb start-server": the adb daemon it
// launches must keep running after WebbyLens exits).
static bool Spawn(const std::wstring& cmdline, HANDLE* process, HANDLE* readPipe, bool detached = false)
{
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0))
        return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = wr;
    si.hStdError = wr;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmd(cmdline.begin(), cmdline.end());
    cmd.push_back(0);
    DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
    if (detached)
        flags |= CREATE_BREAKAWAY_FROM_JOB;   // only matters if WebbyLens itself runs in a job
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &si, &pi);
    if (!ok && detached)
        ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, flags & ~CREATE_BREAKAWAY_FROM_JOB,
                            nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok)
    {
        CloseHandle(rd);
        return false;
    }
    if (!detached && Job())
        AssignProcessToJobObject(Job(), pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    *process = pi.hProcess;
    *readPipe = rd;
    return true;
}

bool RunCapture(const std::wstring& cmdline, std::string* output, DWORD* exitCode, DWORD timeoutMs, bool detached)
{
    HANDLE proc, rd;
    if (!Spawn(cmdline, &proc, &rd, detached))
        return false;

    std::string out;
    std::thread reader([&] {
        char buf[4096];
        DWORD n;
        while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n)
            out.append(buf, n);
    });

    bool finished = WaitForSingleObject(proc, timeoutMs) == WAIT_OBJECT_0;
    if (!finished)
        TerminateProcess(proc, 1);
    // A grandchild (e.g. the adb daemon) could keep the pipe open: don't wait forever.
    if (WaitForSingleObject(reader.native_handle(), 1000) != WAIT_OBJECT_0)
        CancelSynchronousIo(reader.native_handle());
    reader.join();

    DWORD code = 1;
    GetExitCodeProcess(proc, &code);
    CloseHandle(proc);
    CloseHandle(rd);
    if (output) *output = out;
    if (exitCode) *exitCode = code;
    return finished;
}

bool ChildProcess::Start(const std::wstring& cmdline, std::function<void(const std::string&)> onLine)
{
    Kill();
    if (!Spawn(cmdline, &_process, &_readPipe))
        return false;
    HANDLE rd = _readPipe;
    _reader = std::thread([rd, onLine] {
        std::string pending;
        char buf[1024];
        DWORD n;
        while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n)
        {
            pending.append(buf, n);
            size_t pos;
            while ((pos = pending.find('\n')) != std::string::npos)
            {
                std::string line = pending.substr(0, pos);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (onLine) onLine(line);
                pending.erase(0, pos + 1);
            }
        }
        if (!pending.empty() && onLine) onLine(pending);
    });
    return true;
}

bool ChildProcess::IsRunning() const
{
    return _process && WaitForSingleObject(_process, 0) == WAIT_TIMEOUT;
}

void ChildProcess::Kill()
{
    if (_process)
    {
        TerminateProcess(_process, 1);
        WaitForSingleObject(_process, 2000);
    }
    if (_reader.joinable())
    {
        if (WaitForSingleObject(_reader.native_handle(), 1000) != WAIT_OBJECT_0)
            CancelSynchronousIo(_reader.native_handle());
        _reader.join();
    }
    if (_readPipe) { CloseHandle(_readPipe); _readPipe = nullptr; }
    if (_process) { CloseHandle(_process); _process = nullptr; }
}
