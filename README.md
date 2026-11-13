# WebbyLens

**Use your Android phone as a webcam and microphone on Windows 11** — over USB, with
no app to install on the phone. Teams, Zoom, browsers and other programs see a normal
camera called **"WebbyLens"** and a microphone called **"CABLE Output"**.

## Features

- Phone camera as a Windows webcam (1920x1080 or 1280x720, 30 fps), front or back camera
- Phone microphone as a Windows microphone (needs VB-Audio Virtual Cable, see below)
- Four modes, switchable at any time from the tray icon:
  **Webcam**, **Webcam + floating window**, **Floating window only**, **Sound only**
- Floating always-on-top preview: drag to move, mouse wheel to resize, double-click for
  full screen, mirrored by default, adjustable transparency
- Live camera zoom from 0.5x (ultra-wide lens, if the phone has one) up to 8x
- **Pause / resume with Ctrl+Alt+P**: the camera shows a "Paused" picture and the
  microphone is muted in Windows
- Automatic reconnect when the phone is unplugged and plugged in again
- Nothing is sent anywhere: video and audio go only from the phone to your PC

## Requirements

- Windows 11 (the virtual camera uses the Windows 11 camera API)
- Android 12 or newer, with **USB debugging** enabled
  (Settings → About phone → tap "Build number" 7 times → Developer options → USB debugging)
- A USB cable
- For the microphone: [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) (free, install once)

**[User Guide (PDF)](docs/WebbyLens-User-Guide.pdf)** — step-by-step installation and usage.

## Installation

Download the installer from the [Releases](https://github.com/crunchy3d/WebbyLens/releases) page and run it. It installs
WebbyLens, registers the virtual camera and downloads Android's adb tool from Google.
Windows may show "Windows protected your PC" because the installer is not code-signed:
click **More info → Run anyway**.

## Usage

1. Connect the phone by USB and allow USB debugging on the phone when asked.
2. Start **WebbyLens** (it appears as an icon in the taskbar notification area — on
   Windows 11 it may be hidden behind the **^** arrow next to the clock).
3. In Teams/Zoom choose camera **WebbyLens** and microphone **CABLE Output**.

Click the tray icon for the menu (modes, camera, zoom, microphone, floating window,
settings, start with Windows). In the floating window: mouse wheel = size,
Shift+wheel = zoom, Ctrl+wheel = transparency, double-click = full screen,
Esc = leave full screen, right-click = menu.

Logs: `%LOCALAPPDATA%\WebbyLens\logs` (app) and `%ProgramData%\WebbyLens\logs` (camera).

## How it works

```
Android phone                         Windows PC
scrcpy-server  ── USB (adb) ──▶  WebbyLens.exe ──▶ shared memory ──▶ WebbyLensCamera.dll ──▶ Teams, Zoom ...
 camera (H.264)                    decode (Media Foundation)            (Windows 11 virtual camera)
 microphone (PCM)                  audio ──▶ VB-Cable "CABLE Input" ──▶ "CABLE Output" microphone
```

## Building from source

Needs Visual Studio 2022 (or Build Tools) with C++ and the Windows 11 SDK (10.0.22000+).

```
powershell -ExecutionPolicy Bypass -File scripts\fetch-deps.ps1   # scrcpy-server + adb
build.bat                                                          # output in bin\
register.bat                                                       # once, as administrator
```

Then run `bin\WebbyLens.exe` (not from an administrator prompt).

Icons: `python scripts\make_icon.py` (Pillow) → `assets\`. User guide:
`python docs\make_guide.py` (ReportLab, Pillow) → `docs\WebbyLens-User-Guide.pdf`.

The installer (needs [Inno Setup 6.4+](https://jrsoftware.org/isinfo.php)):
`powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1`
→ `installer\output\WebbyLens-Setup-<version>.exe`. The version comes from
`src\common\version.h`. Test tools: `bin\SelfTest.exe` (reads frames from the
camera DLL) and `bin\AudioProbe.exe` (shows the level at "CABLE Output").
If `build.bat` cannot replace `WebbyLensCamera.dll`, the Windows camera service still
has it loaded: run `net stop FrameServer` as administrator.

Source layout: `src/app` (tray app), `src/vcam` (virtual camera DLL), `src/ctl`
(permanent camera install tool), `src/common` (shared code), `src/test`.

## Authors

Damir Vrancic and Claude.

## License

WebbyLens is released under the [MIT License](LICENSE). Third-party components and
their licenses are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
WebbyLens is not affiliated with Google, Android, scrcpy, Microsoft or VB-Audio.
