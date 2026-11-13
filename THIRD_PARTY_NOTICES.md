# Third-party components

WebbyLens itself is licensed under the MIT License (see `LICENSE`). It works
together with the following third-party components.

## scrcpy server — shipped with WebbyLens

- Project: scrcpy by Genymobile — https://github.com/Genymobile/scrcpy
- File: `scrcpy-server` (version 5.0.1), used **unmodified**. It is copied to the
  phone over USB and captures the camera and microphone there.
- License: Apache License 2.0 — full text in `third_party/scrcpy-LICENSE.txt`
  (installed as `licenses\scrcpy-LICENSE.txt`).
- Copyright (C) 2018 Genymobile, Copyright (C) 2018-2026 Romain Vimont.

## Android SDK Platform-Tools (adb) — downloaded, not shipped

- `adb.exe`, `AdbWinApi.dll`, `AdbWinUsbApi.dll` are **not** part of WebbyLens.
  The installer downloads Google's official package from
  https://dl.google.com/android/repository/platform-tools-latest-windows.zip
  during installation; it is provided by Google under its own terms
  (https://developer.android.com/studio/releases/platform-tools).

## VB-Audio Virtual Cable — installed separately by the user

- Needed for the phone microphone. Not distributed with WebbyLens.
- https://vb-audio.com/Cable/ (donationware by VB-Audio Software).

## Acknowledgement

- The Windows 11 virtual camera (`src/vcam`) was written for WebbyLens, using
  Simon Mourier's VCamSample (MIT License, https://github.com/smourier/VCamSample)
  as a reference for how the Windows Frame Server talks to a virtual camera source.
