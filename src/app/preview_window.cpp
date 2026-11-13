#include "preview_window.h"
#include "log.h"
#include <windowsx.h>
#include <cstdio>
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

#define WM_APP_FRAME (WM_APP + 1)
static const wchar_t* kClass = L"WebbyLensPreview";
static const int kMinWidth = 160;
static const int kEdge = 8;      // resize border thickness (pixels)

static RECT WorkAreaFor(HWND hwnd, const RECT* r = nullptr)
{
    HMONITOR mon = r ? MonitorFromRect(r, MONITOR_DEFAULTTONEAREST) : MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    return mi.rcWork;
}

bool PreviewWindow::Create(HINSTANCE inst, const RECT& saved, MenuFn onMenu, CloseFn onClose)
{
    _onMenu = std::move(onMenu);
    _onClose = std::move(onClose);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, _d2d.GetAddressOf());
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)_dwrite.GetAddressOf());
    if (_dwrite)
    {
        _dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                  DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"", &_textFormat);
        if (_textFormat)
        {
            _textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            _textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    RECT r = saved;
    if (r.right - r.left < kMinWidth || r.bottom - r.top < 50)
        r = DefaultRect();
    else
    {
        // make sure the saved position is still on a monitor
        RECT work = WorkAreaFor(nullptr, &r);
        RECT inter;
        if (!IntersectRect(&inter, &r, &work))
            r = DefaultRect();
    }
    _normal = r;

    _hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClass, L"WebbyLens", WS_POPUP,
                            r.left, r.top, r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, this);
    return _hwnd != nullptr;
}

void PreviewWindow::Destroy()
{
    if (_hwnd)
    {
        DestroyWindow(_hwnd);
        _hwnd = nullptr;
    }
}

RECT PreviewWindow::DefaultRect() const
{
    RECT work = WorkAreaFor(nullptr, nullptr);
    POINT pt{ 0, 0 };
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    work = mi.rcWork;
    // 400 x 225 at 100% display scaling, proportionally larger on high-DPI screens
    UINT dpi = _hwnd ? GetDpiForWindow(_hwnd) : GetDpiForSystem();
    if (!dpi) dpi = 96;
    int w = MulDiv(400, dpi, 96), h = (int)(w / _aspect), m = MulDiv(24, dpi, 96);
    return RECT{ work.right - w - m, work.bottom - h - m, work.right - m, work.bottom - m };
}

void PreviewWindow::Show(bool show)
{
    if (!_hwnd) return;
    _visible = show;
    ShowWindow(_hwnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (show)
        InvalidateRect(_hwnd, nullptr, FALSE);
}

RECT PreviewWindow::NormalRect() const
{
    if (_fullscreen || !_hwnd)
        return _normal;
    RECT r;
    GetWindowRect(_hwnd, &r);
    return r;
}

void PreviewWindow::ResetSize()
{
    if (_fullscreen) ToggleFullscreen();
    RECT r = DefaultRect();
    SetWindowPos(_hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void PreviewWindow::SetMirror(bool mirror)
{
    _mirror = mirror;
    if (_hwnd) InvalidateRect(_hwnd, nullptr, FALSE);
}

void PreviewWindow::SetOpacity(int percent)
{
    if (!_hwnd) return;
    LONG_PTR ex = GetWindowLongPtrW(_hwnd, GWL_EXSTYLE);
    if (percent >= 100)
    {
        SetWindowLongPtrW(_hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
    }
    else
    {
        SetWindowLongPtrW(_hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        SetLayeredWindowAttributes(_hwnd, 0, (BYTE)(percent * 255 / 100), LWA_ALPHA);
    }
    InvalidateRect(_hwnd, nullptr, FALSE);
}

void PreviewWindow::SubmitFrame(const Nv12Frame& f)
{
    if (!_visible || !_hwnd)
        return;
    UINT32 w = f.width & ~1u, h = f.height & ~1u;
    {
        std::lock_guard<std::mutex> lk(_frameLock);
        _frame.resize((size_t)w * h * 3 / 2);
        for (UINT32 y = 0; y < h; y++)
            memcpy(_frame.data() + (size_t)y * w, f.y + (size_t)y * f.pitch, w);
        BYTE* uv = _frame.data() + (size_t)w * h;
        for (UINT32 y = 0; y < h / 2; y++)
            memcpy(uv + (size_t)y * w, f.uv + (size_t)y * f.pitch, w);
        _fw = w;
        _fh = h;
        _hasFrame = true;
    }
    // Only mark the window as needing a repaint. WM_PAINT has the lowest priority, so
    // mouse and keyboard input are always handled first, even if drawing is slow.
    // (Posting a message here would starve input when drawing takes longer than a frame.)
    if (!_paintPending.exchange(true))
        InvalidateRect(_hwnd, nullptr, FALSE);
}

void PreviewWindow::ClearFrame()
{
    {
        std::lock_guard<std::mutex> lk(_frameLock);
        _hasFrame = false;
    }
    if (_hwnd)
        InvalidateRect(_hwnd, nullptr, FALSE);
}

void PreviewWindow::SetMessage(const std::wstring& text)
{
    _message = text;
    if (_hwnd) InvalidateRect(_hwnd, nullptr, FALSE);
}

static inline BYTE Clip(int v) { return (BYTE)(v < 0 ? 0 : v > 255 ? 255 : v); }

void PreviewWindow::Render()
{
    if (!_hwnd || !_d2d) return;
    LARGE_INTEGER t0, t1, freq;
    QueryPerformanceCounter(&t0);
    RECT rc;
    GetClientRect(_hwnd, &rc);
    UINT32 cw = (UINT32)(rc.right - rc.left), ch = (UINT32)(rc.bottom - rc.top);
    if (!cw || !ch) return;

    if (!_rt)
    {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
        props.dpiX = props.dpiY = 96.0f;   // 1 DIP = 1 pixel
        if (FAILED(_d2d->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(_hwnd, D2D1::SizeU(cw, ch)), &_rt)))
            return;
        _rt->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.85f, 0.85f), &_textBrush);
        _bmp.Reset();
        _bmpW = _bmpH = 0;
    }
    D2D1_SIZE_U sz = _rt->GetPixelSize();
    if (sz.width != cw || sz.height != ch)
        _rt->Resize(D2D1::SizeU(cw, ch));

    _rt->BeginDraw();
    _rt->Clear(D2D1::ColorF(0.07f, 0.07f, 0.08f));

    bool drawn = false, aspectChanged = false;
    UINT32 dw = 0, dh = 0, dx = 0, dy = 0;   // destination rectangle in the window
    UINT32 bw = 0, bh = 0;                   // bitmap size
    bool gpuScale = false;
    {
        std::lock_guard<std::mutex> lk(_frameLock);
        if (_hasFrame && _fw && _fh)
        {
            double a = (double)_fw / _fh;
            if (fabs(a - _aspect) > 0.01)
            {
                _aspect = a;
                aspectChanged = true;   // window is adjusted after EndDraw
            }
            // fit into the client area, keep aspect ratio
            if ((double)cw / ch > a) { dh = ch; dw = (UINT32)(ch * a); }
            else { dw = cw; dh = (UINT32)(cw / a); }
            dw &= ~1u; dh &= ~1u;
            if (dw >= 2 && dh >= 2)
            {
                dx = (cw - dw) / 2;
                dy = (ch - dh) / 2;
                if (dw < _fw)
                {
                    // smaller than the camera picture: high-quality area downscale on the CPU
                    // (cost proportional to the small window size)
                    bw = dw; bh = dh;
                    _scaled.resize((size_t)bw * bh * 3 / 2);
                    _rsY.Plane(_frame.data(), (int)_fw, (int)_fh, (LONG)_fw, _scaled.data(), (int)bw, (int)bh, (LONG)bw, 1);
                    _rsUV.Plane(_frame.data() + (size_t)_fw * _fh, (int)_fw / 2, (int)_fh / 2, (LONG)_fw,
                                _scaled.data() + (size_t)bw * bh, (int)bw / 2, (int)bh / 2, (LONG)bw, 2);
                }
                else
                {
                    // same size or larger: convert at camera resolution, the GPU enlarges it
                    // (cost does not grow with the window size, e.g. full screen on 4K)
                    bw = _fw; bh = _fh;
                    _scaled.assign(_frame.begin(), _frame.begin() + (size_t)bw * bh * 3 / 2);
                    gpuScale = bw != dw || bh != dh;
                }
                drawn = true;
            }
        }
    }

    if (drawn)
    {
        // NV12 -> BGRA (BT.601 limited range), optionally mirrored
        _bgra.resize((size_t)bw * bh * 4);
        const BYTE* Y = _scaled.data();
        const BYTE* UV = Y + (size_t)bw * bh;
        for (UINT32 y = 0; y < bh; y++)
        {
            const BYTE* yr = Y + (size_t)y * bw;
            const BYTE* uvr = UV + (size_t)(y / 2) * bw;
            UINT32* out = reinterpret_cast<UINT32*>(_bgra.data() + (size_t)y * bw * 4);
            for (UINT32 x = 0; x < bw; x++)
            {
                int c = yr[x] - 16, d = uvr[x & ~1u] - 128, e = uvr[(x & ~1u) + 1] - 128;
                int R = Clip((298 * c + 409 * e + 128) >> 8);
                int G = Clip((298 * c - 100 * d - 208 * e + 128) >> 8);
                int B = Clip((298 * c + 516 * d + 128) >> 8);
                out[_mirror ? bw - 1 - x : x] = 0xFF000000u | (R << 16) | (G << 8) | B;
            }
        }
        if (!_bmp || _bmpW != bw || _bmpH != bh)
        {
            _bmp.Reset();
            D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
            _rt->CreateBitmap(D2D1::SizeU(bw, bh), bp, &_bmp);
            _bmpW = bw;
            _bmpH = bh;
        }
        if (_bmp)
        {
            _bmp->CopyFromMemory(nullptr, _bgra.data(), bw * 4);
            _rt->DrawBitmap(_bmp.Get(), D2D1::RectF((float)dx, (float)dy, (float)(dx + dw), (float)(dy + dh)), 1.0f,
                            gpuScale ? D2D1_BITMAP_INTERPOLATION_MODE_LINEAR : D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        }
    }
    else if (_textFormat && _textBrush)
    {
        _rt->DrawTextW(_message.c_str(), (UINT32)_message.size(), _textFormat.Get(),
                       D2D1::RectF(8, 8, (float)cw - 8, (float)ch - 8), _textBrush.Get());
    }

    if (_rt->EndDraw() == D2DERR_RECREATE_TARGET)
    {
        _rt.Reset();
        _bmp.Reset();
        _textBrush.Reset();
    }

    // drawing time statistics (log every ~10 s)
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&freq);
    double ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
    _renderMsSum += ms;
    _renderMsMax = std::max(_renderMsMax, ms);
    if (++_renderCount == 300)
    {
        char msg[160];
        sprintf_s(msg, "preview: window %ux%u, drawing avg %.1f ms, max %.1f ms", cw, ch,
                  _renderMsSum / _renderCount, _renderMsMax);
        LogLine(msg);
        _renderCount = 0;
        _renderMsSum = _renderMsMax = 0;
    }

    if (aspectChanged && !_fullscreen)
    {
        // picture shape changed (e.g. rotation): give the window the same shape
        RECT wr;
        GetWindowRect(_hwnd, &wr);
        FitAspect(&wr, WMSZ_RIGHT);
        SetWindowPos(_hwnd, nullptr, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void PreviewWindow::FitAspect(RECT* r, int edge)
{
    int w = r->right - r->left, h = r->bottom - r->top;
    switch (edge)
    {
    case WMSZ_TOP:
    case WMSZ_BOTTOM:
        w = (int)(h * _aspect + 0.5);
        r->right = r->left + w;
        break;
    case WMSZ_TOPLEFT:
    case WMSZ_TOPRIGHT:
        h = (int)(w / _aspect + 0.5);
        r->top = r->bottom - h;
        break;
    default:   // left, right, bottom corners
        h = (int)(w / _aspect + 0.5);
        r->bottom = r->top + h;
        break;
    }
}

void PreviewWindow::ZoomAt(POINT pt, double factor)
{
    if (_fullscreen) return;
    RECT r;
    GetWindowRect(_hwnd, &r);
    RECT work = WorkAreaFor(_hwnd);
    int w = r.right - r.left;
    // at most 90% of the screen, so other programs stay reachable (full screen is double-click)
    int maxW = (int)(0.9 * std::min((double)(work.right - work.left), (work.bottom - work.top) * _aspect));
    int nw = std::clamp((int)lround(w * factor), kMinWidth, std::max(kMinWidth, maxW));
    if (nw == w) return;
    int nh = (int)lround(nw / _aspect);
    double ratio = (double)nw / w;
    int left = (int)lround(pt.x - (pt.x - r.left) * ratio);
    int top = (int)lround(pt.y - (pt.y - r.top) * ratio);
    SetWindowPos(_hwnd, nullptr, left, top, nw, nh, SWP_NOZORDER | SWP_NOACTIVATE);
}

void PreviewWindow::ToggleFullscreen()
{
    if (!_fullscreen)
    {
        GetWindowRect(_hwnd, &_normal);
        HMONITOR mon = MonitorFromWindow(_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(mon, &mi);
        _fullscreen = true;
        SetWindowPos(_hwnd, HWND_TOPMOST, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, 0);
        SetForegroundWindow(_hwnd);   // so that Esc reaches us
        SetFocus(_hwnd);
    }
    else
    {
        _fullscreen = false;
        SetWindowPos(_hwnd, HWND_TOPMOST, _normal.left, _normal.top,
                     _normal.right - _normal.left, _normal.bottom - _normal.top, SWP_NOACTIVATE);
    }
}

LRESULT CALLBACK PreviewWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PreviewWindow* self;
    if (msg == WM_NCCREATE)
    {
        self = static_cast<PreviewWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->_hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<PreviewWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT PreviewWindow::Handle(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_PAINT:
        _paintPending = false;
        Render();
        ValidateRect(_hwnd, nullptr);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        if (_rt)
            _rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
        InvalidateRect(_hwnd, nullptr, FALSE);
        return 0;

    case WM_NCHITTEST:
    {
        if (_fullscreen)
            return HTCLIENT;
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT r;
        GetWindowRect(_hwnd, &r);
        bool l = pt.x < r.left + kEdge, rt = pt.x >= r.right - kEdge;
        bool t = pt.y < r.top + kEdge, b = pt.y >= r.bottom - kEdge;
        if (t && l) return HTTOPLEFT;
        if (t && rt) return HTTOPRIGHT;
        if (b && l) return HTBOTTOMLEFT;
        if (b && rt) return HTBOTTOMRIGHT;
        if (l) return HTLEFT;
        if (rt) return HTRIGHT;
        if (t) return HTTOP;
        if (b) return HTBOTTOM;
        return HTCAPTION;   // drag anywhere to move
    }

    case WM_SIZING:
        FitAspect(reinterpret_cast<RECT*>(lp), (int)wp);
        return TRUE;

    case WM_GETMINMAXINFO:
    {
        auto mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = kMinWidth;
        mmi->ptMinTrackSize.y = (LONG)(kMinWidth / _aspect);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        double notches = GET_WHEEL_DELTA_WPARAM(wp) / (double)WHEEL_DELTA;
        WORD keys = GET_KEYSTATE_WPARAM(wp);
        if ((keys & (MK_SHIFT | MK_CONTROL)) && _onWheel)
        {
            // Shift+wheel = camera zoom, Ctrl+wheel = transparency (whole steps only)
            _wheelRemainder += notches;
            int steps = (int)_wheelRemainder;
            _wheelRemainder -= steps;
            if (steps)
                _onWheel((keys & MK_SHIFT) ? WheelAction::CameraZoom : WheelAction::Opacity, steps);
            return 0;
        }
        if (_fullscreen)
        {
            if (notches < 0) ToggleFullscreen();   // scrolling down leaves full screen
            return 0;
        }
        ZoomAt(pt, pow(1.12, notches));
        return 0;
    }

    case WM_NCLBUTTONDBLCLK:
    case WM_LBUTTONDBLCLK:
        ToggleFullscreen();
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && _fullscreen)
            ToggleFullscreen();
        return 0;

    case WM_CONTEXTMENU:
    {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (pt.x == -1 && pt.y == -1)
        {
            RECT r;
            GetWindowRect(_hwnd, &r);
            pt = { r.left + 20, r.top + 20 };
        }
        if (_onMenu) _onMenu(_hwnd, pt);
        return 0;
    }

    case WM_NCRBUTTONUP:
    {
        // right-click on the "caption" (i.e. anywhere): our menu instead of the system menu
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (_onMenu) _onMenu(_hwnd, pt);
        return 0;
    }

    case WM_CLOSE:
        if (_onClose) _onClose();
        return 0;
    }
    return DefWindowProcW(_hwnd, msg, wp, lp);
}
