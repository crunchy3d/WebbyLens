// Child process helpers (adb). All children are placed in a job object so they
// are terminated automatically when WebbyLens.exe exits.
#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <thread>

std::wstring ExeDir();
std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);

// Runs a command line, captures stdout+stderr. Returns false if it could not start
// or timed out (the process is then killed).
bool RunCapture(const std::wstring& cmdline, std::string* output, DWORD* exitCode, DWORD timeoutMs,
                bool detached = false);

// Long-running child whose output lines are passed to a callback.
class ChildProcess
{
public:
    ~ChildProcess() { Kill(); }
    bool Start(const std::wstring& cmdline, std::function<void(const std::string&)> onLine);
    bool IsRunning() const;
    void Kill();

private:
    HANDLE _process = nullptr;
    HANDLE _readPipe = nullptr;
    std::thread _reader;
};
