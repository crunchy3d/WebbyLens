; WebbyLens installer (Inno Setup 6.4+). Build with installer\build-installer.ps1,
; which passes /DAppVersion=x.y.z from src\common\version.h.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define AppName "WebbyLens"
#define AppExe "WebbyLens.exe"
#define Root ".."
#define AppMutexName "Local\WebbyLensApp"
#define AppWindowClass "WebbyLensMain"
#define PlatformToolsUrl "https://dl.google.com/android/repository/platform-tools-latest-windows.zip"

[Setup]
AppId={{48158E96-C7ED-46AA-BCFB-6141447156E3}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Damir Vrancic and Claude
AppPublisherURL=https://github.com/crunchy3d/WebbyLens
AppSupportURL=https://github.com/crunchy3d/WebbyLens/issues
AppUpdatesURL=https://github.com/crunchy3d/WebbyLens/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile={#Root}\LICENSE
OutputDir=output
OutputBaseFilename=WebbyLens-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 11 (the virtual camera API does not exist on Windows 10)
MinVersion=10.0.22000
ArchiveExtraction=full
; a running WebbyLens is closed by the [Code] below (CloseWebbyLens) instead of asking the
; user to exit it from the tray; "Launch WebbyLens" on the last page starts it again
CloseApplications=no
UninstallDisplayIcon={app}\{#AppExe}
SetupIconFile={#Root}\assets\webbylens.ico
UninstallDisplayName={#AppName}
VersionInfoVersion={#AppVersion}
SetupLogging=yes

[Messages]
FinishedLabel=Setup has finished installing [name].%n%nWebbyLens works in the background and runs in the system tray - the notification area next to the clock. On Windows 11 the icon may be hidden behind the small ^ arrow: drag it onto the taskbar to keep it visible. Click the icon to open the menu.%n%nConnect your phone by USB (with USB debugging on) and choose the camera "WebbyLens" and the microphone "CABLE Output" in Teams, Zoom or other programs. The User Guide is in the Start menu.

[Tasks]
Name: autostart; Description: "Start WebbyLens automatically when I sign in to Windows"
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked

[Dirs]
; shared between the app (user) and the camera (Windows camera service): everyone may write
Name: "{commonappdata}\WebbyLens"; Permissions: users-modify
Name: "{commonappdata}\WebbyLens\logs"; Permissions: users-modify

[Files]
Source: "{#Root}\bin\WebbyLens.exe"; DestDir: "{app}"; Flags: ignoreversion
; registered with DllRegisterServer (HKLM CLSID) - the Windows camera service loads it from here
Source: "{#Root}\bin\WebbyLensCamera.dll"; DestDir: "{app}"; Flags: ignoreversion regserver 64bit restartreplace uninsrestartdelete
Source: "{#Root}\bin\WebbyLensCtl.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\bin\AudioProbe.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\third_party\scrcpy-server"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\LICENSE"; DestDir: "{app}\licenses"; DestName: "WebbyLens-LICENSE.txt"
Source: "{#Root}\THIRD_PARTY_NOTICES.md"; DestDir: "{app}\licenses"
Source: "{#Root}\third_party\scrcpy-LICENSE.txt"; DestDir: "{app}\licenses"
Source: "{#Root}\README.md"; DestDir: "{app}"
Source: "{#Root}\docs\WebbyLens-User-Guide.pdf"; DestDir: "{app}"
; Android SDK Platform-Tools (adb), downloaded from Google during setup and unpacked to {app}\platform-tools
Source: "{tmp}\platform-tools.zip"; DestDir: "{app}"; Flags: external extractarchive recursesubdirs ignoreversion; Check: PlatformToolsDownloaded

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autoprograms}\{#AppName} User Guide"; Filename: "{app}\WebbyLens-User-Guide.pdf"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
; "Start with Windows" belongs to the signed-in user: let WebbyLens set it with the user's own rights
Filename: "{app}\{#AppExe}"; Parameters: "--autostart on"; Tasks: autostart; Flags: runasoriginaluser waituntilterminated
Filename: "https://vb-audio.com/Cable/"; Description: "Open the VB-Audio Virtual Cable download page (needed for the phone microphone)"; Flags: shellexec postinstall nowait skipifsilent; Check: not VBCableInstalled
; never run WebbyLens elevated: files it creates must belong to the user
Filename: "{app}\{#AppExe}"; Description: "Launch WebbyLens"; Flags: postinstall nowait skipifsilent runasoriginaluser

[UninstallRun]
; remove a permanent camera (only exists if someone ran "WebbyLensCtl install")
Filename: "{app}\WebbyLensCtl.exe"; Parameters: "uninstall"; Flags: runhidden waituntilterminated; RunOnceId: "RemovePermanentCamera"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\platform-tools"
Type: filesandordirs; Name: "{commonappdata}\WebbyLens"

[Code]
var
  DownloadPage: TDownloadWizardPage;
  PlatformToolsOk: Boolean;

function PlatformToolsDownloaded: Boolean;
begin
  Result := PlatformToolsOk;
end;

function VBCableInstalled: Boolean;
var
  Code: Integer;
begin
  // a VB-Audio Virtual Cable device present in Windows?
  Result := Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoProfile -NonInteractive -Command "if (Get-PnpDevice -FriendlyName ''*VB-Audio Virtual Cable*'' -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }"',
    '', SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0);
end;

function WebbyLensRunning: Boolean;
begin
  // mutex created by WebbyLens.exe (one instance per Windows session)
  Result := CheckForMutexes('{#AppMutexName}');
end;

procedure StopCameraService;
var
  Code: Integer;
begin
  // The Windows camera service keeps WebbyLensCamera.dll loaded; stopping it releases the
  // file. It starts again by itself when an app uses a camera.
  Exec(ExpandConstant('{sys}\net.exe'), 'stop FrameServerMonitor /y', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Exec(ExpandConstant('{sys}\net.exe'), 'stop FrameServer /y', '', SW_HIDE, ewWaitUntilTerminated, Code);
end;

procedure InitializeWizard;
begin
  DownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), SetupMessage(msgPreparingDesc), nil);
  DownloadPage.ShowBaseNameInsteadOfUrl := True;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := MemoDirInfo + NewLine + NewLine;
  if WebbyLensRunning then
    Result := Result + 'WebbyLens is running: Setup will close it and can start it again at the end.' +
      NewLine + NewLine;
  if MemoTasksInfo <> '' then
    Result := Result + MemoTasksInfo + NewLine + NewLine;
  Result := Result + 'Download:' + NewLine + Space +
    'Android SDK Platform-Tools (adb) from Google - dl.google.com' + NewLine + Space +
    '(provided by Google under its own terms; needed to talk to the phone over USB)';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpReady then
  begin
    PlatformToolsOk := False;
    DownloadPage.Clear;
    DownloadPage.Add('{#PlatformToolsUrl}', 'platform-tools.zip', '');
    DownloadPage.Show;
    try
      try
        DownloadPage.Download;
        PlatformToolsOk := True;
      except
        if DownloadPage.AbortedByUser then
          Result := False
        else
          Result := SuppressibleMsgBox(
            'Android Platform-Tools (adb) could not be downloaded:' + #13#10 + GetExceptionMessage + #13#10#13#10 +
            'Continue anyway? WebbyLens needs adb to reach the phone; you can run this setup again later.',
            mbError, MB_YESNO, IDYES) = IDYES;
      end;
    finally
      DownloadPage.Hide;
    end;
  end;
end;

procedure SHChangeNotify(wEventId: Integer; uFlags: Cardinal; dwItem1, dwItem2: Cardinal);
  external 'SHChangeNotify@shell32.dll stdcall';

procedure CurStepChanged(CurStep: TSetupStep);
begin
  // Explorer caches icons by file path: after an update with a new icon, make it reload them.
  if CurStep = ssPostInstall then
    SHChangeNotify($08000000 {SHCNE_ASSOCCHANGED}, 0 {SHCNF_IDLIST}, 0, 0);
end;

procedure StopAdb;
var
  Code: Integer;
begin
  // WebbyLens leaves the adb background server running (it keeps the phone ready); it locks
  // {app}\platform-tools\adb.exe. Any adb client starts it again when needed.
  if FileExists(ExpandConstant('{app}\platform-tools\adb.exe')) then
    Exec(ExpandConstant('{app}\platform-tools\adb.exe'), 'kill-server', '', SW_HIDE, ewWaitUntilTerminated, Code);
end;

function CloseWebbyLens: Boolean;
var
  Wnd: HWND;
  I: Integer;
begin
  // Ask WebbyLens to exit like "Exit" in its tray menu: WM_CLOSE to its hidden main window
  // saves the settings, stops the phone stream and unmutes the microphone if paused.
  Result := not WebbyLensRunning;
  if Result then
    Exit;
  Wnd := FindWindowByClassName('{#AppWindowClass}');
  if Wnd <> 0 then
  begin
    Log('Closing the running WebbyLens');
    PostMessage(Wnd, $0010 {WM_CLOSE}, 0, 0);
  end;
  for I := 1 to 150 do
  begin
    if not WebbyLensRunning then
    begin
      Result := True;
      Exit;
    end;
    Sleep(100);
  end;
  Log('WebbyLens did not exit within 15 s');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  if not CloseWebbyLens then
  begin
    Result := 'WebbyLens is still running. Exit it (right-click its tray icon - Exit) and click Back, then Install again.';
    Exit;
  end;
  StopAdb;             // allows replacing adb.exe on an update
  StopCameraService;   // allows replacing WebbyLensCamera.dll on an update
  Result := '';
end;

function InitializeUninstall: Boolean;
begin
  // close a running WebbyLens first; returning False cancels the uninstall cleanly
  Result := True;
  while Result and not CloseWebbyLens do
    Result := SuppressibleMsgBox('WebbyLens is still running. Exit it (right-click its tray icon - Exit), then click OK.',
                                 mbError, MB_OKCANCEL, IDOK) = IDOK;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    StopAdb;
    StopCameraService;
    RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'WebbyLens');
    if SuppressibleMsgBox('Also remove your WebbyLens settings and logs?', mbConfirmation, MB_YESNO, IDNO) = IDYES then
    begin
      RegDeleteKeyIncludingSubkeys(HKCU, 'Software\WebbyLens');
      DelTree(ExpandConstant('{localappdata}\WebbyLens'), True, True, True);
    end;
  end;
end;
