#pragma once
#include <windows.h>
#include <string>

std::wstring LogDir();   // %LOCALAPPDATA%\WebbyLens\logs (created if missing)
void LogOpen(const std::wstring& path, bool echoToConsole);
void LogLine(const std::string& line);   // file (and console if verbose)
void Status(const std::string& line);    // always console + file
std::string HrStr(HRESULT hr);
