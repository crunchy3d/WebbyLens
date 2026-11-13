@echo off
rem Builds WebbyLens into bin\ (or %WEBBYLENS_OUT%):
rem   WebbyLens.exe        tray application
rem   WebbyLensCamera.dll  virtual camera (Media Foundation source)
rem   WebbyLensCtl.exe     camera control tool (permanent install/uninstall)
rem   SelfTest.exe, AudioProbe.exe  test tools
rem Third-party files (scrcpy-server, adb) are fetched with scripts\fetch-deps.ps1.
setlocal
set ROOT=%~dp0
set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" (
    for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat
)
call "%VCVARS%" >nul || (echo vcvars64.bat not found & exit /b 1)

rem WEBBYLENS_OUT can redirect the output (e.g. to a staging folder while bin\ is in use)
if defined WEBBYLENS_OUT (set OUT=%WEBBYLENS_OUT%) else (set OUT=%ROOT%bin)
set OBJ=%ROOT%build\obj
for %%d in ("%OUT%" "%OBJ%\vcam" "%OBJ%\ctl" "%OBJ%\app" "%OBJ%\test") do if not exist %%d mkdir %%d

set CFLAGS=/nologo /std:c++17 /utf-8 /EHsc /O2 /W3 /MT /Zi /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN
set RCFLAGS=/nologo /I "%ROOT%src\common" /I "%ROOT%assets"

echo === WebbyLensCamera.dll ===
rc %RCFLAGS% /fo "%OBJ%\vcam\camera.res" "%ROOT%src\vcam\camera.rc" || exit /b 1
rem A DLL loaded by the camera service cannot be overwritten, but it can be renamed:
rem the service keeps the old copy until it restarts, new activations use the new file.
del /q "%OUT%\WebbyLensCamera.dll.old*" >nul 2>&1
if exist "%OUT%\WebbyLensCamera.dll" (
    move /y "%OUT%\WebbyLensCamera.dll" "%OUT%\WebbyLensCamera.dll.old%RANDOM%" >nul 2>&1
)
cl %CFLAGS% /LD /Fo"%OBJ%\vcam\\" /Fd"%OBJ%\vcam\\" "%ROOT%src\vcam\*.cpp" "%OBJ%\vcam\camera.res" /Fe"%OUT%\WebbyLensCamera.dll" ^
   /link /DEF:"%ROOT%src\vcam\WebbyLensCamera.def" /DEBUG /OPT:REF ^
   mfplat.lib mfuuid.lib mfsensorgroup.lib ole32.lib advapi32.lib propsys.lib
if errorlevel 1 (
    echo.
    echo WARNING: WebbyLensCamera.dll was NOT rebuilt. If the error above is LNK1104
    echo "cannot open file", the DLL is in use by the Windows camera service: close
    echo camera apps, or run "net stop FrameServer" as administrator.
    echo Continuing with the other programs...
    echo.
)

echo === WebbyLensCtl.exe ===
rc %RCFLAGS% /fo "%OBJ%\ctl\ctl.res" "%ROOT%src\ctl\ctl.rc" || exit /b 1
cl %CFLAGS% /Fo"%OBJ%\ctl\\" /Fd"%OBJ%\ctl\\" "%ROOT%src\ctl\WebbyLensCtl.cpp" "%OBJ%\ctl\ctl.res" /Fe"%OUT%\WebbyLensCtl.exe" ^
   /link /DEBUG mfplat.lib mfsensorgroup.lib ole32.lib advapi32.lib
if errorlevel 1 echo WARNING: WebbyLensCtl.exe was NOT rebuilt (is it running?). Continuing...

echo === WebbyLens.exe ===
rc %RCFLAGS% /fo "%OBJ%\app\app.res" "%ROOT%src\app\app.rc" || exit /b 1
cl %CFLAGS% /Fo"%OBJ%\app\\" /Fd"%OBJ%\app\\" "%ROOT%src\app\*.cpp" "%OBJ%\app\app.res" /Fe"%OUT%\WebbyLens.exe" ^
   /link /DEBUG /SUBSYSTEM:WINDOWS /MANIFEST:EMBED mfplat.lib mf.lib mfuuid.lib mfsensorgroup.lib ole32.lib oleaut32.lib ^
   ws2_32.lib strmiids.lib avrt.lib user32.lib gdi32.lib shell32.lib advapi32.lib d2d1.lib dwrite.lib comctl32.lib
if errorlevel 1 exit /b 1

rem third-party runtime files (see scripts\fetch-deps.ps1)
if exist "%ROOT%third_party\scrcpy-server" copy /y "%ROOT%third_party\scrcpy-server" "%OUT%\" >nul
if exist "%ROOT%third_party\platform-tools\adb.exe" if not exist "%OUT%\platform-tools\adb.exe" (
    xcopy /e /i /q /y "%ROOT%third_party\platform-tools" "%OUT%\platform-tools" >nul
)
if not exist "%OUT%\scrcpy-server" echo NOTE: scrcpy-server missing - run: powershell -ExecutionPolicy Bypass -File scripts\fetch-deps.ps1

echo === SelfTest.exe ===
cl %CFLAGS% /Fo"%OBJ%\test\\" /Fd"%OBJ%\test\\" "%ROOT%src\test\SelfTest.cpp" "%ROOT%src\app\frame_writer.cpp" "%ROOT%src\app\log.cpp" "%ROOT%src\app\process.cpp" /Fe"%OUT%\SelfTest.exe" ^
   /link /DEBUG mfplat.lib mfreadwrite.lib mfuuid.lib ole32.lib advapi32.lib
if errorlevel 1 exit /b 1

echo === AudioProbe.exe ===
cl %CFLAGS% /Fo"%OBJ%\test\\" /Fd"%OBJ%\test\\" "%ROOT%src\test\AudioProbe.cpp" /Fe"%OUT%\AudioProbe.exe" ^
   /link /DEBUG ole32.lib
if errorlevel 1 exit /b 1

echo.
echo Build OK: %OUT%
