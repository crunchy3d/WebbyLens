@echo off
rem Registers the WebbyLens media source (needs administrator rights; asks for them).
net session >nul 2>&1
if errorlevel 1 (
    powershell -NoProfile -Command "Start-Process -Verb RunAs -FilePath '%~f0'"
    exit /b
)
regsvr32 /s "%~dp0bin\WebbyLensCamera.dll"
if errorlevel 1 (echo Registration FAILED.) else (echo WebbyLens media source registered.)
pause
