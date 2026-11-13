#include "settings_window.h"
#include <commctrl.h>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
    enum : int
    {
        IDC_FACING = 2001, IDC_ZOOM, IDC_ZOOM_VALUE, IDC_RES, IDC_ROT, IDC_MIC, IDC_NOISE,
        IDC_OPACITY, IDC_OPACITY_VALUE, IDC_MIRROR, IDC_AUTO, IDC_CLOSE,
    };

    const double kZoomFactor = 1.0 + 1.0 / 16.0;   // must match the phone's zoom step
    const int kZoomMin = -11, kZoomMax = 34;        // 0.51x .. 7.9x

    HWND g_wnd;
    AppSettings* g_s;
    SettingsCallbacks g_cb;
    HFONT g_font;
    bool g_zoomDragging = false;
    bool g_updating = false;

    int ZoomToPos(float z) { return (int)lround(log((double)z) / log(kZoomFactor)); }
    float PosToZoom(int p) { return (float)pow(kZoomFactor, p); }

    HWND Ctl(int id) { return GetDlgItem(g_wnd, id); }

    void UpdateLabels()
    {
        wchar_t buf[32];
        int zp = (int)SendMessageW(Ctl(IDC_ZOOM), TBM_GETPOS, 0, 0);
        swprintf_s(buf, L"%.2fx", PosToZoom(zp));
        SetWindowTextW(Ctl(IDC_ZOOM_VALUE), buf);
        int tp = (int)SendMessageW(Ctl(IDC_OPACITY), TBM_GETPOS, 0, 0);
        swprintf_s(buf, L"%d %%", tp);
        SetWindowTextW(Ctl(IDC_OPACITY_VALUE), buf);
    }

    void Load()
    {
        g_updating = true;
        // camera, zoom, resolution, rotation and microphone type are remembered per phone
        std::wstring title = L"WebbyLens Settings";
        if (!g_s->phoneName.empty()) title += L" - " + g_s->phoneName;
        SetWindowTextW(g_wnd, title.c_str());
        SendMessageW(Ctl(IDC_FACING), CB_SETCURSEL, g_s->facing == "back" ? 1 : 0, 0);
        if (!g_zoomDragging)
            SendMessageW(Ctl(IDC_ZOOM), TBM_SETPOS, TRUE, ZoomToPos(g_s->zoom));
        SendMessageW(Ctl(IDC_RES), CB_SETCURSEL, g_s->maxSize == 1280 ? 1 : 0, 0);
        SendMessageW(Ctl(IDC_ROT), CB_SETCURSEL, g_s->rotation / 90, 0);
        CheckDlgButton(g_wnd, IDC_MIC, g_s->mic ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(g_wnd, IDC_NOISE, g_s->micSource != "mic" ? BST_CHECKED : BST_UNCHECKED);
        SendMessageW(Ctl(IDC_OPACITY), TBM_SETPOS, TRUE, 100 - g_s->opacity);   // slider shows transparency
        CheckDlgButton(g_wnd, IDC_MIRROR, g_s->mirror ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(g_wnd, IDC_AUTO, g_s->autoStandby ? BST_CHECKED : BST_UNCHECKED);
        UpdateLabels();
        g_updating = false;
    }

    LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_HSCROLL:
        {
            HWND bar = (HWND)lp;
            int pos = (int)SendMessageW(bar, TBM_GETPOS, 0, 0);
            int code = LOWORD(wp);
            if (bar == Ctl(IDC_ZOOM))
            {
                g_zoomDragging = code == TB_THUMBTRACK;
                if (!g_updating && g_cb.setZoom) g_cb.setZoom(PosToZoom(pos));
            }
            else if (bar == Ctl(IDC_OPACITY))
            {
                if (!g_updating && g_cb.setOpacity) g_cb.setOpacity(100 - pos);
            }
            UpdateLabels();
            return 0;
        }
        case WM_COMMAND:
        {
            int id = LOWORD(wp), code = HIWORD(wp);
            if (g_updating) return 0;
            bool changed = true;
            switch (id)
            {
            case IDC_FACING:
                if (code != CBN_SELCHANGE) return 0;
                g_s->facing = SendMessageW(Ctl(IDC_FACING), CB_GETCURSEL, 0, 0) == 1 ? "back" : "front";
                break;
            case IDC_RES:
                if (code != CBN_SELCHANGE) return 0;
                g_s->maxSize = SendMessageW(Ctl(IDC_RES), CB_GETCURSEL, 0, 0) == 1 ? 1280 : 1920;
                break;
            case IDC_ROT:
                if (code != CBN_SELCHANGE) return 0;
                g_s->rotation = (int)SendMessageW(Ctl(IDC_ROT), CB_GETCURSEL, 0, 0) * 90;
                break;
            case IDC_MIC: g_s->mic = IsDlgButtonChecked(hwnd, IDC_MIC) == BST_CHECKED; break;
            case IDC_NOISE:
                g_s->micSource = IsDlgButtonChecked(hwnd, IDC_NOISE) == BST_CHECKED ? "mic-voice-communication" : "mic";
                break;
            case IDC_MIRROR: g_s->mirror = IsDlgButtonChecked(hwnd, IDC_MIRROR) == BST_CHECKED; break;
            case IDC_AUTO: g_s->autoStandby = IsDlgButtonChecked(hwnd, IDC_AUTO) == BST_CHECKED; break;
            case IDC_CLOSE: case IDCANCEL: DestroyWindow(hwnd); return 0;
            default: changed = false;
            }
            if (changed && g_cb.changed) g_cb.changed();
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            g_wnd = nullptr;
            if (g_font) { DeleteObject(g_font); g_font = nullptr; }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void SyncSettingsWindow()
{
    if (g_wnd) Load();
}

void ShowSettingsWindow(HINSTANCE inst, AppSettings* settings, SettingsCallbacks callbacks)
{
    g_s = settings;
    g_cb = std::move(callbacks);
    if (g_wnd)
    {
        Load();
        ShowWindow(g_wnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_wnd);
        return;
    }

    static bool registered = false;
    if (!registered)
    {
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icc);
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = Proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
        wc.lpszClassName = L"WebbyLensSettings";
        RegisterClassExW(&wc);
        registered = true;
    }

    UINT dpi = GetDpiForSystem();
    auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };
    const int W = 420, rowH = 34, labelW = 130, x2 = 16 + labelW;
    int rows = 11;
    RECT r{ 0, 0, S(W), S(16 + rows * rowH + 50) };
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0, dpi);
    g_wnd = CreateWindowExW(0, L"WebbyLensSettings", L"WebbyLens Settings", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                            CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                            nullptr, nullptr, inst, nullptr);

    g_font = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
        HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h),
                                 g_wnd, (HMENU)(INT_PTR)id, inst, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
        return c;
    };
    auto label = [&](const wchar_t* text, int row) { make(L"STATIC", text, SS_LEFT, 16, 16 + row * rowH + 4, labelW, 20, -1); };
    int y = 16;

    label(L"Camera", 0);
    HWND facing = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, x2, y, 200, 200, IDC_FACING);
    SendMessageW(facing, CB_ADDSTRING, 0, (LPARAM)L"Front camera");
    SendMessageW(facing, CB_ADDSTRING, 0, (LPARAM)L"Back camera");
    y += rowH;

    label(L"Zoom", 1);
    HWND zoom = make(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, x2 - 4, y, 200, 26, IDC_ZOOM);
    SendMessageW(zoom, TBM_SETRANGE, TRUE, MAKELPARAM(kZoomMin, kZoomMax));
    SendMessageW(zoom, TBM_SETPAGESIZE, 0, 4);
    make(L"STATIC", L"", SS_LEFT, x2 + 202, y + 4, 60, 20, IDC_ZOOM_VALUE);
    y += rowH;

    label(L"Resolution", 2);
    HWND res = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, x2, y, 200, 200, IDC_RES);
    SendMessageW(res, CB_ADDSTRING, 0, (LPARAM)L"1920 x 1080");
    SendMessageW(res, CB_ADDSTRING, 0, (LPARAM)L"1280 x 720");
    y += rowH;

    label(L"Rotation", 3);
    HWND rot = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, x2, y, 200, 200, IDC_ROT);
    for (const wchar_t* t : { L"0°", L"90°", L"180°", L"270°" })
        SendMessageW(rot, CB_ADDSTRING, 0, (LPARAM)t);
    y += rowH;

    label(L"Microphone", 4);
    make(L"BUTTON", L"Phone microphone on", BS_AUTOCHECKBOX | WS_TABSTOP, x2, y, 250, 24, IDC_MIC);
    y += rowH;
    make(L"BUTTON", L"Noise suppression (phone)", BS_AUTOCHECKBOX | WS_TABSTOP, x2, y, 250, 24, IDC_NOISE);
    y += rowH;

    label(L"Window transparency", 6);
    HWND op = make(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, x2 - 4, y, 200, 26, IDC_OPACITY);
    SendMessageW(op, TBM_SETRANGE, TRUE, MAKELPARAM(0, 80));
    SendMessageW(op, TBM_SETPAGESIZE, 0, 10);
    make(L"STATIC", L"", SS_LEFT, x2 + 202, y + 4, 60, 20, IDC_OPACITY_VALUE);
    y += rowH;

    make(L"BUTTON", L"Mirror the floating window", BS_AUTOCHECKBOX | WS_TABSTOP, x2, y, 250, 24, IDC_MIRROR);
    y += rowH;

    label(L"Phone", 8);
    make(L"BUTTON", L"Stream only when used (auto standby)", BS_AUTOCHECKBOX | WS_TABSTOP, x2, y, 260, 24, IDC_AUTO);
    y += rowH;

    make(L"STATIC",
         L"Floating window: wheel = size, Shift+wheel = zoom, Ctrl+wheel = transparency, "
         L"double-click = full screen. Ctrl+Alt+P = pause / resume.",
         SS_LEFT, 16, y, W - 32, 2 * rowH - 8, -1);
    y += 2 * rowH;

    make(L"BUTTON", L"Close", BS_DEFPUSHBUTTON | WS_TABSTOP, W - 16 - 90, y, 90, 28, IDC_CLOSE);

    Load();
    ShowWindow(g_wnd, SW_SHOWNORMAL);
    SetForegroundWindow(g_wnd);
}
