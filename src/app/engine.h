// Background engine: keeps the phone connected (reconnecting as needed), decodes
// video and routes it to the virtual camera and/or the floating window, and plays
// the phone microphone into VB-Cable.
#pragma once
#include <windows.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include "scrcpy_client.h"
#include "h264_decoder.h"
#include "frame_writer.h"
#include "audio_player.h"

struct EngineStatus
{
    enum State { Idle, Waiting, Connecting, StreamingVideo, StreamingSound, Paused, Standby } state = Idle;
    std::string text;
};

class Engine
{
public:
    using StatusFn = std::function<void(const EngineStatus&)>;
    using FrameFn = std::function<void(const Nv12Frame&)>;
    using ZoomFn = std::function<void(float)>;   // zoom actually applied by the phone
    // A phone was found (before streaming starts, name empty, opt may be adjusted) and later
    // connected (name = its model, opt = nullptr). Called on the engine thread.
    using PhoneFn = std::function<void(const std::wstring& serial, const std::string& name, StreamOptions* opt)>;
    void SetPhoneHandler(PhoneFn fn) { _onPhone = std::move(fn); }   // before Start

    bool Start(StatusFn onStatus, FrameFn onPreviewFrame, ZoomFn onZoom, std::string* error);
    void Stop();

    // Applies new settings. The phone connection is restarted only if stream options changed.
    void Configure(const StreamOptions& opt, bool toWebcam, bool toPreview);

    // Pause: phone connection closed, camera shows "Paused", no audio.
    void SetPaused(bool paused);
    bool Paused() const { return _paused; }

    // Standby (automatic): phone connection closed because nothing uses camera or microphone.
    void SetStandby(bool standby);
    bool Standby() const { return _standby; }
    bool CameraInUse() const { return _writer.ConsumerActive(); }

    // Live camera zoom (no reconnect). Zoom steps are factor 1.0625 (scrcpy).
    void SetZoom(float zoom);
    void ZoomSteps(int steps);   // + = in, - = out
    static int ZoomIndex(float zoom);
    static float ZoomFromIndex(int index);

    bool AudioReady() const { return _player.Running(); }
    const std::string& AudioError() const { return _audioError; }

private:
    void Loop();
    void Report(EngineStatus::State s, const std::string& text);
    static bool SameStream(const StreamOptions& a, const StreamOptions& b);

    StatusFn _onStatus;
    FrameFn _onPreview;
    ZoomFn _onZoom;
    PhoneFn _onPhone;
    std::atomic<bool> _paused{ false };
    std::atomic<bool> _standby{ false };
    std::atomic<int> _zoomIndex{ 0 };   // last commanded / reported zoom step
    ScrcpyClient _client;
    H264Decoder _decoder;
    FrameWriter _writer;
    AudioPlayer _player;
    std::string _audioError;

    std::thread _thread;
    HANDLE _wake = nullptr;
    std::mutex _cfgLock;
    StreamOptions _opt;
    bool _configured = false;
    std::atomic<bool> _restart{ false };
    std::atomic<bool> _quit{ false };
    std::atomic<bool> _toWebcam{ true };
    std::atomic<bool> _toPreview{ false };

    // per-connection statistics (engine thread)
    bool _announced = false;
    LONGLONG _statsStart = 0;
    UINT64 _statsFrames = 0;
    std::string _connDesc;
};
