// User settings, stored in HKEY_CURRENT_USER\Software\WebbyLens.
// Phone-specific settings (camera, zoom, resolution, rotation, microphone type) are also kept
// per phone in ...\WebbyLens\Phones\<adb serial>; the others are the same for all phones.
#pragma once
#include <windows.h>
#include <string>

enum class Mode : int
{
    Webcam = 0,         // phone video -> virtual camera
    WebcamWindow = 1,   // + floating window
    WindowOnly = 2,     // floating window, virtual camera shows "no signal"
    SoundOnly = 3,      // no video at all, only the microphone
};

inline bool ModeHasWebcam(Mode m) { return m == Mode::Webcam || m == Mode::WebcamWindow; }
inline bool ModeHasWindow(Mode m) { return m == Mode::WebcamWindow || m == Mode::WindowOnly; }
inline bool ModeHasVideo(Mode m) { return m != Mode::SoundOnly; }

struct AppSettings
{
    Mode mode = Mode::Webcam;
    // --- per phone ---
    std::string facing = "front";       // front | back
    float zoom = 1.0f;                  // 1.0 = main lens, 0.5 = ultra-wide
    int maxSize = 1920;                 // 1920 (1080p) or 1280 (720p)
    int rotation = 0;                   // 0, 90, 180, 270
    std::string micSource = "mic";      // mic | mic-voice-communication
    std::wstring phone;                 // adb serial of the current (last connected) phone
    std::wstring phoneName;             // its model name, e.g. "Pixel 7a"
    // --- same for all phones ---
    bool mic = true;
    bool autoStandby = true;            // stream only while camera/microphone/window is in use
    bool mirror = true;                 // floating window only
    int opacity = 100;                  // floating window, percent
    RECT window = { 0, 0, 0, 0 };       // floating window position (empty = default)

    void Load();
    void Save() const;            // also saves the per-phone values for the current phone
    // Loads the per-phone values stored for this phone; false (nothing changed) if it is new.
    bool LoadPhone(const std::wstring& serial);
};

bool AutostartEnabled();
void SetAutostart(bool on);
