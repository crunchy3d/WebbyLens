#include "engine.h"
#include "log.h"
#include <mfapi.h>
#include <mfidl.h>
#include <cmath>
#include <vector>

static const double kZoomFactor = 1.0 + 1.0 / 16.0;   // scrcpy's zoom step

int Engine::ZoomIndex(float zoom)
{
    return (int)lround(log((double)zoom) / log(kZoomFactor));
}

float Engine::ZoomFromIndex(int index)
{
    return (float)pow(kZoomFactor, index);
}

bool Engine::Start(StatusFn onStatus, FrameFn onPreviewFrame, ZoomFn onZoom, std::string* error)
{
    _onStatus = std::move(onStatus);
    _onPreview = std::move(onPreviewFrame);
    _onZoom = std::move(onZoom);

    // The phone reports every zoom change it applies (also the clamped start value).
    _client.SetServerLineHandler([this](const std::string& line) {
        const char* key = "Set camera zoom: ";
        size_t pos = line.find(key);
        if (pos == std::string::npos) return;
        float z = (float)atof(line.c_str() + pos + strlen(key));
        if (z <= 0) return;
        _zoomIndex = ZoomIndex(z);
        if (_onZoom) _onZoom(z);
    });

    // Phone identified: let the app apply the settings remembered for it. The engine's target
    // options follow, so the app's next Configure with the same values does not reconnect.
    _client.SetDeviceHook([this](const std::wstring& serial, StreamOptions* o) {
        if (_onPhone) _onPhone(serial, "", o);
        {
            std::lock_guard<std::mutex> lk(_cfgLock);
            _opt.facing = o->facing;
            _opt.zoom = o->zoom;
            _opt.maxSize = o->maxSize;
            _opt.orientation = o->orientation;
            _opt.audioSource = o->audioSource;
        }
        _zoomIndex = ZoomIndex(o->zoom > 0 ? o->zoom : 1.0f);
    });

    if (!_writer.Open(error))
        return false;

    std::string dev;
    if (_player.Start(L"CABLE Input", &dev, &_audioError))
    {
        _audioError.clear();
        LogLine("audio output: " + dev);
    }
    else
    {
        LogLine("audio disabled: " + _audioError);
    }

    if (!_decoder.Init([this](const Nv12Frame& f) {
            if (_toWebcam) _writer.Write(f);
            if (_toPreview && _onPreview) _onPreview(f);

            _statsFrames++;
            LONGLONG now = MFGetSystemTime();
            if (!_statsStart) _statsStart = now;
            double secs = (now - _statsStart) / 1e7;
            if (!_announced && secs >= 1)
            {
                char msg[160];
                sprintf_s(msg, "%s - %ux%u, %.0f fps", _connDesc.c_str(), f.width, f.height, _statsFrames / secs);
                Report(EngineStatus::StreamingVideo, msg);
                _announced = true;
            }
            if (secs >= 30)
            {
                char msg[128];
                sprintf_s(msg, "stats: %ux%u %.1f fps", f.width, f.height, _statsFrames / secs);
                LogLine(msg);
                _statsStart = now;
                _statsFrames = 0;
            }
        }))
    {
        *error = "cannot create the H.264 decoder";
        return false;
    }

    _wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    _quit = false;
    _thread = std::thread(&Engine::Loop, this);
    return true;
}

void Engine::Stop()
{
    _quit = true;
    if (_wake) SetEvent(_wake);
    _client.Interrupt();
    if (_thread.joinable())
        _thread.join();
    _player.Stop();
    _writer.Close();
    _decoder.Close();
    if (_wake) { CloseHandle(_wake); _wake = nullptr; }
}

bool Engine::SameStream(const StreamOptions& a, const StreamOptions& b)
{
    // zoom is not compared: it is changed live through the control channel
    return a.serial == b.serial && a.facing == b.facing && a.cameraId == b.cameraId && a.size == b.size &&
           a.aspect == b.aspect && a.orientation == b.orientation && a.fps == b.fps &&
           a.maxSize == b.maxSize && a.bitRate == b.bitRate && a.video == b.video && a.audio == b.audio &&
           a.audioSource == b.audioSource;
}

void Engine::Configure(const StreamOptions& opt, bool toWebcam, bool toPreview)
{
    bool wasWebcam = _toWebcam.exchange(toWebcam);
    _toPreview = toPreview;
    if (wasWebcam && !toWebcam)
        _writer.SetNoSignal();   // virtual camera -> "no signal" immediately

    bool restart;
    {
        std::lock_guard<std::mutex> lk(_cfgLock);
        restart = _configured && !SameStream(_opt, opt);
        _opt = opt;
        _configured = true;
    }
    if (restart)
    {
        LogLine("settings changed: reconnecting");
        _restart = true;
        _client.Interrupt();
    }
    if (_wake) SetEvent(_wake);
}

void Engine::SetPaused(bool paused)
{
    if (_paused.exchange(paused) == paused)
        return;
    _writer.SetPaused(paused);
    if (paused)
    {
        _restart = true;        // leave the current connection
        _client.Interrupt();
    }
    if (_wake) SetEvent(_wake);
}

void Engine::SetStandby(bool standby)
{
    if (_standby.exchange(standby) == standby)
        return;
    LogLine(standby ? "auto standby: nothing uses camera or microphone - phone stream stopped"
                    : "auto standby: in use - starting the phone stream");
    if (standby)
    {
        _restart = true;
        _client.Interrupt();
    }
    if (_wake) SetEvent(_wake);
}

void Engine::SetZoom(float zoom)
{
    {
        std::lock_guard<std::mutex> lk(_cfgLock);
        _opt.zoom = zoom;       // used for the next connection
    }
    ZoomSteps(ZoomIndex(zoom) - _zoomIndex);
}

void Engine::ZoomSteps(int steps)
{
    if (!steps || !_client.HasControl())
        return;
    for (int i = 0; i < abs(steps); i++)
        _client.SendZoom(steps > 0);
    _zoomIndex += steps;   // corrected when the phone reports the applied value
}

void Engine::Report(EngineStatus::State s, const std::string& text)
{
    LogLine("status: " + text);
    if (_onStatus)
        _onStatus(EngineStatus{ s, text });
}

void Engine::Loop()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::string lastError;

    while (!_quit)
    {
        StreamOptions opt;
        bool configured;
        {
            std::lock_guard<std::mutex> lk(_cfgLock);
            opt = _opt;
            configured = _configured;
            _restart = false;
        }
        if (!configured)
        {
            WaitForSingleObject(_wake, INFINITE);
            continue;
        }
        if (_paused)
        {
            Report(EngineStatus::Paused, "Paused (Ctrl+Alt+P to resume)");
            lastError.clear();
            while (_paused && !_quit)
                WaitForSingleObject(_wake, INFINITE);
            continue;
        }
        if (_standby)
        {
            Report(EngineStatus::Standby, "Standby - starts when an app uses the camera or microphone");
            lastError.clear();
            while (_standby && !_paused && !_quit)
                WaitForSingleObject(_wake, INFINITE);
            continue;
        }
        if (opt.audio && !_player.Running())
            opt.audio = false;   // no VB-Cable: video only
        if (!opt.video && !opt.audio)
        {
            Report(EngineStatus::Idle, _player.Running() ? "Sound only, but the microphone is switched off"
                                                         : "Sound only needs VB-Audio Virtual Cable");
            WaitForSingleObject(_wake, INFINITE);
            continue;
        }

        std::string err;
        if (lastError.empty())
            Report(EngineStatus::Connecting, "Connecting to the phone...");
        _zoomIndex = ZoomIndex(opt.zoom > 0 ? opt.zoom : 1.0f);
        if (!_client.Connect(opt, &err))
        {
            if (_quit) break;
            if (err != lastError)
            {
                Report(EngineStatus::Waiting, err);
                lastError = err;
            }
            WaitForSingleObject(_wake, 2000);
            continue;
        }
        lastError.clear();
        if (_restart || _quit)
        {
            _client.Disconnect();
            continue;
        }

        opt = _client.Options();   // may differ: settings of this phone
        if (_onPhone) _onPhone(_client.Serial(), _client.DeviceName(), nullptr);
        std::string camera = opt.video ? (opt.facing == "back" ? "back camera" : "front camera") : "microphone only";
        _connDesc = _client.DeviceName() + ", " + camera;
        if (opt.audio && !_client.HasAudio())
            LogLine("audio unavailable: " + _client.AudioNote());

        // audio: phone -> jitter buffer -> CABLE Input
        std::atomic<bool> audioEnded{ false };
        std::thread audioThread;
        if (_client.HasAudio())
        {
            _player.Clear();
            audioThread = std::thread([&] {
                AudioPacket ap;
                while (_client.ReadAudioPacket(&ap))
                    _player.Push(ap.data.data(), ap.data.size());
                audioEnded = true;
                SetEvent(_wake);
            });
        }

        if (opt.video)
        {
            _decoder.Reset();
            _announced = false;
            _statsStart = 0;
            _statsFrames = 0;
            std::vector<BYTE> config, merged;
            VideoPacket p;
            int decodeErrors = 0;
            while (!_quit && !_restart && _client.ReadPacket(&p))
            {
                if (p.session || p.data.empty())
                    continue;
                if (p.config)
                {
                    config = p.data;   // SPS/PPS, prepended to the next frame
                    continue;
                }
                bool ok;
                if (!config.empty())
                {
                    merged = config;
                    merged.insert(merged.end(), p.data.begin(), p.data.end());
                    config.clear();
                    ok = _decoder.Decode(merged.data(), merged.size(), p.pts);
                }
                else
                {
                    ok = _decoder.Decode(p.data.data(), p.data.size(), p.pts);
                }
                if (!ok && ++decodeErrors == 20)
                {
                    LogLine("too many decoder errors, reconnecting");
                    break;
                }
            }
        }
        else
        {
            Report(EngineStatus::StreamingSound, _connDesc);
            while (!_quit && !_restart && !audioEnded)
                WaitForSingleObject(_wake, 1000);
        }

        _client.Interrupt();
        if (audioThread.joinable())
            audioThread.join();
        _client.Disconnect();
        _player.Clear();
        if (!_paused)
            _writer.SetNoSignal();
        if (!_quit && !_restart)
            Report(EngineStatus::Waiting, "Phone disconnected - waiting for it...");
    }
    CoUninitialize();
}
