// Windows-level mute of a recording device (used for "CABLE Output" while paused,
// so Teams/Zoom see a muted microphone instead of silent audio).
#pragma once
#include <string>

bool SetRecordingDeviceMute(const std::wstring& nameMatch, bool mute);

// true if some program is currently recording from that device (e.g. Teams in a meeting)
bool RecordingDeviceInUse(const std::wstring& nameMatch);

// Mute/unmute "CABLE Output" and remember in the registry that WebbyLens muted it,
// so a crash while paused cannot leave the microphone muted forever.
void PauseMicMute(bool mute);
void RestoreMicIfLeftMuted();   // call at startup
