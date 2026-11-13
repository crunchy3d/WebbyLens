@echo off
rem Removes the permanent camera (if installed) and unregisters the media source.
net session >nul 2>&1
if errorlevel 1 (
    powershell -NoProfile -Command "Start-Process -Verb RunAs -FilePath '%~f0'"
    exit /b
)
"%~dp0bin\WebbyLensCtl.exe" uninstall
regsvr32 /s /u "%~dp0bin\WebbyLensCamera.dll"
if errorlevel 1 (echo Unregistration FAILED.) else (echo WebbyLens media source unregistered.)
pause
