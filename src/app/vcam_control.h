// Creating the "WebbyLens" virtual camera from the app (replaces "WebbyLensCtl run").
#pragma once
#include <windows.h>
#include <mfvirtualcamera.h>

bool VcamSourceRegistered();          // WebbyLensCamera.dll registered in HKLM (register.bat)
bool VcamDeviceExists();              // a camera named "WebbyLens" is already visible
HRESULT VcamCreateSession(IMFVirtualCamera** cam);   // exists while this process runs
