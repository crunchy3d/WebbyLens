// Plays the phone microphone (48 kHz, stereo, 16-bit) into one specific Windows
// playback device - the VB-Cable "CABLE Input" - never into the default speakers.
// Apps (Teams, Zoom) then use the matching recording device "CABLE Output" as microphone.
#pragma once
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class AudioPlayer
{
public:
    ~AudioPlayer() { Stop(); }

    // Finds an active playback device whose name contains `nameMatch` (case-insensitive).
    bool Start(const std::wstring& nameMatch, std::string* deviceName, std::string* error);
    void Stop();
    bool Running() const { return _thread.joinable(); }

    void Push(const BYTE* pcm, size_t bytes);   // s16 stereo frames
    void Clear();                               // e.g. after reconnect

    static std::vector<std::wstring> ListDevices();

private:
    void Run();
    size_t Pull(INT16* out, size_t frames);     // returns frames delivered (rest = silence)

    std::wstring _deviceId;
    std::thread _thread;
    std::atomic<bool> _stop{ false };
    HANDLE _ready = nullptr;
    HRESULT _startResult = S_OK;

    // ring buffer of interleaved stereo samples
    std::mutex _lock;
    std::vector<INT16> _ring;
    size_t _read = 0, _count = 0;     // in frames
    bool _primed = false;
    UINT64 _underruns = 0, _dropped = 0;
};
