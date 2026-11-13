// Floating always-on-top window with the phone picture.
//   drag = move, edges/corners = resize (aspect ratio kept), mouse wheel = zoom
//   around the pointer, double-click = full screen on/off, Esc = leave full screen,
//   right-click = menu (provided by the app).
#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include "h264_decoder.h"
#include "../common/resample.h"

class PreviewWindow
{
public:
    using MenuFn = std::function<void(HWND, POINT)>;
    using CloseFn = std::function<void()>;
    enum class WheelAction { CameraZoom, Opacity };
    using WheelFn = std::function<void(WheelAction, int notches)>;
    void SetWheelHandler(WheelFn fn) { _onWheel = std::move(fn); }

    bool Create(HINSTANCE inst, const RECT& saved, MenuFn onMenu, CloseFn onClose);
    void Destroy();
    void Show(bool show);
    bool Visible() const { return _visible; }
    HWND Hwnd() const { return _hwnd; }

    void SubmitFrame(const Nv12Frame& f);         // any thread
    void SetMessage(const std::wstring& text);    // shown while there is no picture
    void ClearFrame();                            // any thread
    void SetMirror(bool mirror);
    void SetOpacity(int percent);
    void ResetSize();
    bool IsFullscreen() const { return _fullscreen; }
    void ExitFullscreen() { if (_fullscreen) ToggleFullscreen(); }
    RECT NormalRect() const;                      // position to save (not the full-screen one)

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Render();
    void ToggleFullscreen();
    void ZoomAt(POINT screenPt, double factor);
    void FitAspect(RECT* r, int edge);
    RECT DefaultRect() const;

    HWND _hwnd = nullptr;
    MenuFn _onMenu;
    CloseFn _onClose;
    WheelFn _onWheel;
    double _wheelRemainder = 0;   // for smooth (high-resolution) wheels
    std::atomic<bool> _visible{ false };
    std::atomic<bool> _paintPending{ false };
    bool _fullscreen = false;
    RECT _normal{};
    bool _mirror = true;
    double _aspect = 16.0 / 9.0;
    std::wstring _message = L"Waiting for the phone...";

    // latest frame (NV12, tight)
    std::mutex _frameLock;
    std::vector<BYTE> _frame;
    UINT32 _fw = 0, _fh = 0;
    bool _hasFrame = false;

    // rendering
    Microsoft::WRL::ComPtr<ID2D1Factory> _d2d;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> _rt;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> _bmp;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> _textBrush;
    Microsoft::WRL::ComPtr<IDWriteFactory> _dwrite;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> _textFormat;
    UINT32 _bmpW = 0, _bmpH = 0;
    Resampler _rsY, _rsUV;
    std::vector<BYTE> _scaled, _bgra;
    int _renderCount = 0;
    double _renderMsSum = 0, _renderMsMax = 0;
};
