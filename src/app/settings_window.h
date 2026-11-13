// "Settings" window: all options in one place, every change applies immediately.
#pragma once
#include <windows.h>
#include <functional>
#include "settings.h"

struct SettingsCallbacks
{
    std::function<void()> changed;          // AppSettings modified (camera, mic, mirror, ...)
    std::function<void(float)> setZoom;     // live camera zoom
    std::function<void(int)> setOpacity;    // floating window opacity in percent
};

void ShowSettingsWindow(HINSTANCE inst, AppSettings* settings, SettingsCallbacks callbacks);
void SyncSettingsWindow();                  // refresh the controls (e.g. zoom reported by the phone)
