# Changelog

All notable changes are listed here. Versions follow [Semantic Versioning](https://semver.org/).

## [0.1.0] - 2026-11-13

First version.

- Android phone (Android 12+, USB) as a Windows 11 webcam: virtual camera "WebbyLens"
  usable in Teams, Zoom and other apps; 1920x1080 / 1280x720, 30 fps.
- Phone microphone as a Windows microphone through VB-Audio Virtual Cable ("CABLE Output").
- Four modes: Webcam, Webcam + floating window, Floating window only, Sound only.
- Floating always-on-top window: drag, mouse-wheel size, full screen, mirror, transparency.
- Live camera zoom (0.5x ultra-wide to 8x), front/back camera, rotation.
- Pause / resume with Ctrl+Alt+P ("Paused" picture, microphone muted in Windows).
- Tray icon with menu, settings window, start with Windows, automatic reconnect.
- Automatic standby (default on): the phone streams only while an app uses the WebbyLens
  camera or the CABLE Output microphone, or the floating window is open.
- Clear message when the phone camera is busy (e.g. the phone's Camera app is open).
- Settings remembered per phone: camera, zoom, resolution, rotation and noise suppression
  follow the phone that is connected; a new phone starts with the current settings.
