// WebbyLens.exe - Android phone as webcam + microphone. Runs in the system tray.
//
// Modes (tray menu or right-click on the floating window):
//   Webcam / Webcam + floating window / Floating window only / Sound only
//
// Command line (optional):
//   --serial S             use this phone when several are connected
//   --verbose              detailed log also to a console window
//   --list-cameras         print the phone cameras and exit
//   --list-audio-devices   print the playback devices and exit
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <mfapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include "engine.h"
#include "preview_window.h"
#include "settings.h"
#include "vcam_control.h"
#include "audio_mute.h"
#include "settings_window.h"
#include "log.h"
#include "../common/version.h"

#define WEBBYLENS_WIDEN2(x) L##x
#define WEBBYLENS_WIDEN(x) WEBBYLENS_WIDEN2(x)
#define WEBBYLENS_STRW WEBBYLENS_WIDEN(WEBBYLENS_VERSION_STRING)

// modern look for the settings window controls
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' " \
    "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using Microsoft::WRL::ComPtr;

#define WM_APP_TRAY   (WM_APP + 10)
#define WM_APP_STATUS (WM_APP + 11)
#define WM_APP_ZOOM   (WM_APP + 12)   // lParam = zoom * 1000 reported by the phone
#define WM_APP_PHONE  (WM_APP + 13)   // lParam = new PhoneEvent: a phone was found / connected

struct PhoneEvent { std::wstring serial; std::string name; };

enum : UINT
{
    ID_STATUS = 1000,
    ID_MODE_WEBCAM = 1001, ID_MODE_WEBCAM_WINDOW, ID_MODE_WINDOW, ID_MODE_SOUND,
    ID_CAM_FRONT = 1101, ID_CAM_BACK,
    ID_PAUSE = 1050, ID_SETTINGS, ID_AUTOSTANDBY,
    ID_ZOOM_05 = 1201, ID_ZOOM_10, ID_ZOOM_15, ID_ZOOM_20, ID_ZOOM_IN, ID_ZOOM_OUT,
    ID_RES_1080 = 1301, ID_RES_720,
    ID_ROT_0 = 1401, ID_ROT_90, ID_ROT_180, ID_ROT_270,
    ID_MIC_ON = 1501, ID_MIC_STANDARD, ID_MIC_VOICE,
    ID_WIN_MIRROR = 1601, ID_OPACITY_100, ID_OPACITY_85, ID_OPACITY_70, ID_WIN_RESET,
    ID_WIN_EXIT_FULLSCREEN, ID_WIN_CLOSE,
    ID_AUTOSTART = 1701, ID_OPEN_LOG, ID_EXIT,
};

static HINSTANCE g_inst;
static HWND g_main;
static AppSettings g_settings;
static Engine g_engine;
static PreviewWindow g_preview;
static ComPtr<IMFVirtualCamera> g_vcam;
static NOTIFYICONDATAW g_nid{};
static HICON g_icons[4];          // 0 = waiting, 1 = video, 2 = sound only, 3 = paused
static EngineStatus g_status{ EngineStatus::Connecting, "Starting..." };
static UINT g_taskbarCreated;
static std::wstring g_serial;
static bool g_quitting = false;
static bool g_paused = false;
static void AutoStandbyTick();

// ---------------------------------------------------------------------------
// Tray icons: embedded resources 101-104 (assets\tray-*.ico, made by scripts\make_icon.py)

static HICON LoadTrayIcon(int id)
{
    HICON icon = nullptr;
    LoadIconMetric(g_inst, MAKEINTRESOURCEW(id), LIM_SMALL, &icon);   // right size for the DPI
    return icon;
}


static const char* ModeName(Mode m)
{
    switch (m)
    {
    case Mode::Webcam: return "Webcam";
    case Mode::WebcamWindow: return "Webcam + floating window";
    case Mode::WindowOnly: return "Floating window only";
    default: return "Sound only";
    }
}

static void UpdateTray()
{
    int icon = 0;
    if (g_paused) icon = 3;
    else if (g_status.state == EngineStatus::StreamingVideo) icon = 1;
    else if (g_status.state == EngineStatus::StreamingSound) icon = 2;
    g_nid.hIcon = g_icons[icon];
    std::wstring tip = L"WebbyLens - " + Utf8ToWide(ModeName(g_settings.mode)) + L"\n" +
                       (g_paused ? std::wstring(L"Paused (Ctrl+Alt+P to resume)") : Utf8ToWide(g_status.text));
    if (tip.size() > 127) tip = tip.substr(0, 124) + L"...";
    wcscpy_s(g_nid.szTip, tip.c_str());
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void AddTray()
{
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_main;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = g_icons[0];
    wcscpy_s(g_nid.szTip, L"WebbyLens");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    UpdateTray();
}

// ---------------------------------------------------------------------------
// Settings -> engine / window

// the per-phone part of the stream options
static void PhoneOptions(const AppSettings& s, StreamOptions* o)
{
    o->facing = s.facing;
    o->zoom = s.zoom;
    o->maxSize = s.maxSize;
    o->orientation = s.rotation ? std::to_string(s.rotation) : "";
    o->audioSource = s.micSource;
}

static void ApplySettings(bool save = true)
{
    StreamOptions o;
    o.serial = g_serial;
    PhoneOptions(g_settings, &o);
    o.aspect = "16:9";
    o.video = ModeHasVideo(g_settings.mode);
    o.audio = g_settings.mic;

    bool window = ModeHasWindow(g_settings.mode) && !g_paused;
    g_preview.SetMirror(g_settings.mirror);
    g_preview.SetOpacity(g_settings.opacity);
    if (!window)
        g_preview.ClearFrame();
    g_engine.Configure(o, ModeHasWebcam(g_settings.mode), window);
    AutoStandbyTick();
    g_preview.Show(window);
    UpdateTray();

    if (save)
    {
        g_settings.window = g_preview.NormalRect();
        g_settings.Save();
    }
}

static void TogglePause()
{
    g_paused = !g_paused;
    LogLine(g_paused ? "=== paused ===" : "=== resumed ===");
    if (g_paused)
    {
        g_preview.ExitFullscreen();
        g_engine.SetPaused(true);
        PauseMicMute(true);       // Teams/Zoom see a muted microphone
    }
    else
    {
        PauseMicMute(false);
        g_engine.SetPaused(false);
    }
    ApplySettings(false);         // hides / restores the floating window
}

// Automatic standby: called once per second. The phone streams while the floating window is
// open, an app receives frames from the WebbyLens camera, or an app records from CABLE Output.
// Starts immediately when needed, stops after 15 s without use.
static ULONGLONG g_lastUse = 0;

static void AutoStandbyTick()
{
    if (!g_settings.autoStandby || g_paused)
    {
        if (g_engine.Standby()) g_engine.SetStandby(false);
        return;
    }
    bool window = ModeHasWindow(g_settings.mode);
    bool camera = ModeHasWebcam(g_settings.mode) && g_engine.CameraInUse();
    bool mic = g_settings.mic && g_engine.AudioReady() && RecordingDeviceInUse(L"CABLE Output");
    ULONGLONG now = GetTickCount64();
    if (window || camera || mic)
    {
        g_lastUse = now;
        if (g_engine.Standby()) g_engine.SetStandby(false);
    }
    else if (!g_engine.Standby() && now - g_lastUse > 15000)
    {
        g_engine.SetStandby(true);
    }
}

static void SetZoomValue(float z)
{
    z = std::max(0.5f, std::min(8.0f, z));
    g_settings.zoom = z;
    g_engine.SetZoom(z);          // live if streaming, otherwise used at the next start
    g_settings.Save();
    SyncSettingsWindow();
}

static void SetOpacityValue(int percent)
{
    g_settings.opacity = std::max(20, std::min(100, percent));
    g_preview.SetOpacity(g_settings.opacity);
    g_settings.Save();
    SyncSettingsWindow();
}

static void OpenSettings()
{
    SettingsCallbacks cb;
    cb.changed = [] { ApplySettings(); };
    cb.setZoom = [](float z) { SetZoomValue(z); };
    cb.setOpacity = [](int p) { SetOpacityValue(p); };
    ShowSettingsWindow(g_inst, &g_settings, cb);
}

static void OnPreviewClosed()
{
    // closing the window = switch to the same mode without the window
    if (g_settings.mode == Mode::WebcamWindow) g_settings.mode = Mode::Webcam;
    else if (g_settings.mode == Mode::WindowOnly) g_settings.mode = Mode::SoundOnly;
    ApplySettings();
}

// ---------------------------------------------------------------------------
// Menu (same for the tray icon and the floating window)

static void Check(HMENU m, UINT id, bool on) { CheckMenuItem(m, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); }
static void Radio(HMENU m, UINT first, UINT last, UINT sel) { CheckMenuRadioItem(m, first, last, sel, MF_BYCOMMAND); }

static void ShowMenu(POINT pt)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, ID_STATUS, L"WebbyLens " WEBBYLENS_STRW);
    std::wstring status = Utf8ToWide(g_status.text);
    if (status.size() > 70) status = status.substr(0, 67) + L"...";
    AppendMenuW(menu, MF_STRING | MF_GRAYED, ID_STATUS, status.c_str());
    if (!g_engine.AudioReady())
        AppendMenuW(menu, MF_STRING | MF_GRAYED, ID_STATUS, L"Microphone: VB-Audio Virtual Cable not found");
    if (!g_vcam && !VcamDeviceExists())
        AppendMenuW(menu, MF_STRING | MF_GRAYED, ID_STATUS, L"Virtual camera not available (run register.bat)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING, ID_PAUSE, g_paused ? L"Resume\tCtrl+Alt+P" : L"Pause\tCtrl+Alt+P");
    AppendMenuW(menu, MF_STRING, ID_AUTOSTANDBY, L"Stream only when used (auto standby)");
    Check(menu, ID_AUTOSTANDBY, g_settings.autoStandby);
    AppendMenuW(menu, MF_STRING, ID_SETTINGS, L"Settings...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // always reachable ways back from a too large / full-screen window
    if (g_preview.Visible())
    {
        if (g_preview.IsFullscreen())
            AppendMenuW(menu, MF_STRING, ID_WIN_EXIT_FULLSCREEN, L"Exit full screen\tEsc");
        AppendMenuW(menu, MF_STRING, ID_WIN_CLOSE, L"Close floating window");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }

    AppendMenuW(menu, MF_STRING, ID_MODE_WEBCAM, L"Webcam");
    AppendMenuW(menu, MF_STRING, ID_MODE_WEBCAM_WINDOW, L"Webcam + floating window");
    AppendMenuW(menu, MF_STRING, ID_MODE_WINDOW, L"Floating window only");
    AppendMenuW(menu, MF_STRING, ID_MODE_SOUND, L"Sound only (microphone)");
    Radio(menu, ID_MODE_WEBCAM, ID_MODE_SOUND, ID_MODE_WEBCAM + (UINT)g_settings.mode);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    HMENU cam = CreatePopupMenu();
    if (!g_settings.phoneName.empty() || !g_settings.phone.empty())
    {
        std::wstring who = L"Settings of " + (g_settings.phoneName.empty() ? g_settings.phone : g_settings.phoneName);
        AppendMenuW(cam, MF_STRING | MF_GRAYED, ID_STATUS, who.c_str());
        AppendMenuW(cam, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(cam, MF_STRING, ID_CAM_FRONT, L"Front camera");
    AppendMenuW(cam, MF_STRING, ID_CAM_BACK, L"Back camera");
    Radio(cam, ID_CAM_FRONT, ID_CAM_BACK, g_settings.facing == "back" ? ID_CAM_BACK : ID_CAM_FRONT);
    AppendMenuW(cam, MF_SEPARATOR, 0, nullptr);
    wchar_t zoomNow[64];
    swprintf_s(zoomNow, L"Zoom: %.2fx", g_settings.zoom);
    AppendMenuW(cam, MF_STRING | MF_GRAYED, ID_STATUS, zoomNow);
    AppendMenuW(cam, MF_STRING, ID_ZOOM_IN, L"Zoom in\tShift+wheel");
    AppendMenuW(cam, MF_STRING, ID_ZOOM_OUT, L"Zoom out\tShift+wheel");
    AppendMenuW(cam, MF_STRING, ID_ZOOM_05, L"Zoom 0.5x (ultra-wide lens, back)");
    AppendMenuW(cam, MF_STRING, ID_ZOOM_10, L"Zoom 1x");
    AppendMenuW(cam, MF_STRING, ID_ZOOM_15, L"Zoom 1.5x");
    AppendMenuW(cam, MF_STRING, ID_ZOOM_20, L"Zoom 2x");
    AppendMenuW(cam, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(cam, MF_STRING, ID_RES_1080, L"1920 x 1080");
    AppendMenuW(cam, MF_STRING, ID_RES_720, L"1280 x 720");
    Radio(cam, ID_RES_1080, ID_RES_720, g_settings.maxSize == 1280 ? ID_RES_720 : ID_RES_1080);
    AppendMenuW(cam, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(cam, MF_STRING, ID_ROT_0, L"Rotation 0°");
    AppendMenuW(cam, MF_STRING, ID_ROT_90, L"Rotation 90°");
    AppendMenuW(cam, MF_STRING, ID_ROT_180, L"Rotation 180°");
    AppendMenuW(cam, MF_STRING, ID_ROT_270, L"Rotation 270°");
    Radio(cam, ID_ROT_0, ID_ROT_270, ID_ROT_0 + g_settings.rotation / 90);
    std::wstring camLabel = L"Camera";
    if (!g_settings.phoneName.empty()) camLabel += L" (" + g_settings.phoneName + L")";
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)cam, camLabel.c_str());

    HMENU mic = CreatePopupMenu();
    AppendMenuW(mic, MF_STRING, ID_MIC_ON, L"Phone microphone on");
    Check(mic, ID_MIC_ON, g_settings.mic);
    AppendMenuW(mic, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(mic, MF_STRING, ID_MIC_STANDARD, L"Standard");
    AppendMenuW(mic, MF_STRING, ID_MIC_VOICE, L"Noise suppression (phone)");
    Radio(mic, ID_MIC_STANDARD, ID_MIC_VOICE, g_settings.micSource == "mic" ? ID_MIC_STANDARD : ID_MIC_VOICE);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)mic, L"Microphone");

    HMENU win = CreatePopupMenu();
    AppendMenuW(win, MF_STRING, ID_WIN_MIRROR, L"Mirror");
    Check(win, ID_WIN_MIRROR, g_settings.mirror);
    AppendMenuW(win, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(win, MF_STRING, ID_OPACITY_100, L"Opaque");
    AppendMenuW(win, MF_STRING, ID_OPACITY_85, L"Transparency 15%");
    AppendMenuW(win, MF_STRING, ID_OPACITY_70, L"Transparency 30%");
    Radio(win, ID_OPACITY_100, ID_OPACITY_70,
          g_settings.opacity >= 100 ? ID_OPACITY_100 : g_settings.opacity >= 85 ? ID_OPACITY_85 : ID_OPACITY_70);
    AppendMenuW(win, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(win, MF_STRING, ID_WIN_RESET, L"Reset size and position");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)win, L"Floating window");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_AUTOSTART, L"Start with Windows");
    Check(menu, ID_AUTOSTART, AutostartEnabled());
    AppendMenuW(menu, MF_STRING, ID_OPEN_LOG, L"Open log folder");
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");

    SetForegroundWindow(g_main);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, g_main, nullptr);
    PostMessageW(g_main, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (!cmd) return;

    switch (cmd)
    {
    case ID_MODE_WEBCAM: case ID_MODE_WEBCAM_WINDOW: case ID_MODE_WINDOW: case ID_MODE_SOUND:
        g_settings.mode = (Mode)(cmd - ID_MODE_WEBCAM); break;
    case ID_CAM_FRONT: g_settings.facing = "front"; break;
    case ID_CAM_BACK: g_settings.facing = "back"; break;
    case ID_PAUSE: TogglePause(); return;
    case ID_AUTOSTANDBY:
        g_settings.autoStandby = !g_settings.autoStandby;
        g_settings.Save();
        g_lastUse = GetTickCount64();
        AutoStandbyTick();
        SyncSettingsWindow();
        return;
    case ID_SETTINGS: OpenSettings(); return;
    case ID_ZOOM_05: SetZoomValue(0.5f); return;
    case ID_ZOOM_10: SetZoomValue(1.0f); return;
    case ID_ZOOM_15: SetZoomValue(1.5f); return;
    case ID_ZOOM_20: SetZoomValue(2.0f); return;
    case ID_ZOOM_IN: SetZoomValue(Engine::ZoomFromIndex(Engine::ZoomIndex(g_settings.zoom) + 1)); return;
    case ID_ZOOM_OUT: SetZoomValue(Engine::ZoomFromIndex(Engine::ZoomIndex(g_settings.zoom) - 1)); return;
    case ID_RES_1080: g_settings.maxSize = 1920; break;
    case ID_RES_720: g_settings.maxSize = 1280; break;
    case ID_ROT_0: case ID_ROT_90: case ID_ROT_180: case ID_ROT_270:
        g_settings.rotation = (int)(cmd - ID_ROT_0) * 90; break;
    case ID_MIC_ON: g_settings.mic = !g_settings.mic; break;
    case ID_MIC_STANDARD: g_settings.micSource = "mic"; break;
    case ID_MIC_VOICE: g_settings.micSource = "mic-voice-communication"; break;
    case ID_WIN_MIRROR: g_settings.mirror = !g_settings.mirror; break;
    case ID_OPACITY_100: g_settings.opacity = 100; break;
    case ID_OPACITY_85: g_settings.opacity = 85; break;
    case ID_OPACITY_70: g_settings.opacity = 70; break;
    case ID_WIN_RESET: g_preview.ResetSize(); break;
    case ID_WIN_EXIT_FULLSCREEN: g_preview.ExitFullscreen(); return;
    case ID_WIN_CLOSE: g_preview.ExitFullscreen(); OnPreviewClosed(); return;
    case ID_AUTOSTART: SetAutostart(!AutostartEnabled()); return;
    case ID_OPEN_LOG:
        ShellExecuteW(nullptr, L"open", LogDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    case ID_EXIT:
        DestroyWindow(g_main);
        return;
    }
    ApplySettings();
}

// ---------------------------------------------------------------------------

static void Shutdown()
{
    if (g_quitting) return;
    g_quitting = true;
    g_settings.window = g_preview.NormalRect();
    g_settings.Save();
    g_engine.Stop();
    if (g_vcam)
    {
        g_vcam->Remove();
        g_vcam.Reset();
    }
    if (g_paused)
        PauseMicMute(false);   // never leave CABLE Output muted
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_preview.Destroy();
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_APP_TRAY:
        if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            ShowMenu(pt);
        }
        return 0;

    case WM_APP_STATUS:
    {
        std::unique_ptr<EngineStatus> s(reinterpret_cast<EngineStatus*>(lp));
        g_status = *s;
        if (g_status.state != EngineStatus::StreamingVideo)
        {
            g_preview.ClearFrame();
            g_preview.SetMessage(Utf8ToWide(g_status.text));
        }
        UpdateTray();
        return 0;
    }

    case WM_TIMER:
        if (wp == 1) AutoStandbyTick();
        return 0;

    case WM_HOTKEY:
        // Ctrl+Alt+P: pause / resume (pausing also closes the floating window)
        TogglePause();
        return 0;

    case WM_APP_PHONE:
    {
        // A phone was found (the engine already started it with its remembered settings) or
        // connected (now its name is known). From here on, changes are saved for this phone.
        std::unique_ptr<PhoneEvent> e(reinterpret_cast<PhoneEvent*>(lp));
        if (e->serial != g_settings.phone)
        {
            g_settings.phone = e->serial;
            g_settings.phoneName.clear();
            if (g_settings.LoadPhone(e->serial))
                LogLine("phone " + WideToUtf8(e->serial) + ": using its remembered settings");
            else
                LogLine("new phone " + WideToUtf8(e->serial) + ": starting with the current settings");
            ApplySettings();      // saves; no reconnect (the engine already uses these values)
            SyncSettingsWindow();
        }
        if (!e->name.empty() && Utf8ToWide(e->name) != g_settings.phoneName)
        {
            g_settings.phoneName = Utf8ToWide(e->name);
            g_settings.Save();
            SyncSettingsWindow();
        }
        return 0;
    }

    case WM_APP_ZOOM:
        // zoom actually applied by the phone (may be clamped to the camera's range)
        g_settings.zoom = (float)lp / 1000.0f;
        g_settings.Save();
        SyncSettingsWindow();
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp) Shutdown();
        return 0;

    case WM_DESTROY:
        Shutdown();
        PostQuitMessage(0);
        return 0;
    }
    if (msg == g_taskbarCreated && msg)
    {
        AddTray();   // Explorer restarted
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void AttachParentConsole()
{
    if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole())
    {
        FILE* f;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        printf("\n");
    }
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
    g_inst = inst;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    bool verbose = false, listCams = false, listAudio = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; i++)
    {
        std::wstring a = argv[i];
        if (a == L"--serial" && i + 1 < argc) g_serial = argv[++i];
        else if (a == L"--verbose") verbose = true;
        else if (a == L"--list-cameras") listCams = true;
        else if (a == L"--list-audio-devices") listAudio = true;
        else if (a == L"--autostart" && i + 1 < argc)
        {
            // used by the installer (run with the user's own rights): set and exit
            SetAutostart(std::wstring(argv[i + 1]) == L"on");
            LocalFree(argv);
            return 0;
        }
    }
    LocalFree(argv);

    if (verbose || listCams || listAudio)
        AttachParentConsole();
    LogOpen(LogDir() + L"\\WebbyLens.log", verbose);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION);

    if (listAudio)
    {
        printf("Playback devices:\n");
        for (auto& n : AudioPlayer::ListDevices())
            printf("  %s\n", WideToUtf8(n).c_str());
        return 0;
    }
    if (listCams)
    {
        ScrcpyClient client;
        StreamOptions opt;
        opt.serial = g_serial;
        std::string out, err;
        if (client.ListCameras(opt, &out, &err)) printf("%s\n", out.c_str());
        else printf("%s\n", err.c_str());
        return 0;
    }

    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\WebbyLensApp");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(nullptr, L"WebbyLens is already running - see its icon in the taskbar notification area.",
                    L"WebbyLens", MB_ICONINFORMATION);
        return 0;
    }

    LogLine("=== WebbyLens " WEBBYLENS_VERSION_STRING " started ===");
    g_settings.Load();

    RestoreMicIfLeftMuted();

    g_icons[0] = LoadTrayIcon(102);   // standby / waiting
    g_icons[1] = LoadTrayIcon(101);   // live video
    g_icons[2] = LoadTrayIcon(103);   // sound only
    g_icons[3] = LoadTrayIcon(104);   // paused

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"WebbyLensMain";
    RegisterClassExW(&wc);
    g_main = CreateWindowExW(0, wc.lpszClassName, L"WebbyLens", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    AddTray();
    if (!RegisterHotKey(g_main, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'P'))
        LogLine("hotkey Ctrl+Alt+P is used by another program");

    // virtual camera (exists while WebbyLens runs, unless it was installed permanently)
    if (!VcamSourceRegistered())
    {
        MessageBoxW(nullptr,
            L"The WebbyLens virtual camera is not registered yet.\n\n"
            L"Run register.bat once (it asks for administrator rights).\n"
            L"Until then, the floating window and the microphone still work.",
            L"WebbyLens", MB_ICONWARNING);
    }
    else if (!VcamDeviceExists())
    {
        HRESULT hr = VcamCreateSession(&g_vcam);
        LogLine("virtual camera: " + std::string(SUCCEEDED(hr) ? "created" : "FAILED " + HrStr(hr)));
    }
    else
    {
        LogLine("virtual camera: already present (permanent install or WebbyLensCtl)");
    }

    g_preview.Create(inst, g_settings.window, [](HWND, POINT pt) { ShowMenu(pt); }, OnPreviewClosed);
    g_preview.SetWheelHandler([](PreviewWindow::WheelAction action, int steps) {
        if (action == PreviewWindow::WheelAction::CameraZoom)
            SetZoomValue(Engine::ZoomFromIndex(Engine::ZoomIndex(g_settings.zoom) + steps));
        else
            SetOpacityValue(g_settings.opacity + 5 * steps);
    });

    std::string err;
    // runs on the engine thread: only reads the registry, the UI thread takes over via WM_APP_PHONE
    g_engine.SetPhoneHandler([](const std::wstring& serial, const std::string& name, StreamOptions* o) {
        if (o)
        {
            AppSettings phone;
            if (phone.LoadPhone(serial))
                PhoneOptions(phone, o);
        }
        PostMessageW(g_main, WM_APP_PHONE, 0, (LPARAM) new PhoneEvent{ serial, name });
    });
    if (!g_engine.Start(
            [](const EngineStatus& s) { PostMessageW(g_main, WM_APP_STATUS, 0, (LPARAM) new EngineStatus(s)); },
            [](const Nv12Frame& f) { g_preview.SubmitFrame(f); },
            [](float z) { PostMessageW(g_main, WM_APP_ZOOM, 0, (LPARAM)lround(z * 1000)); },
            &err))
    {
        MessageBoxW(nullptr, Utf8ToWide("WebbyLens cannot start: " + err).c_str(), L"WebbyLens", MB_ICONERROR);
        Shutdown();
        return 1;
    }
    ApplySettings(false);       // also decides standby right away
    SetTimer(g_main, 1, 1000, nullptr);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    MFShutdown();
    CoUninitialize();
    WSACleanup();
    if (single) CloseHandle(single);
    return 0;
}
