// Frame exchange between WebbyLens.exe (writer, user session) and
// WebbyLensCamera.dll (reader, runs inside the Windows Frame Server service).
//
// A memory-mapped file in %ProgramData%\WebbyLens is used instead of a named
// section: the two processes live in different sessions and a normal user is
// not allowed to create Global\ objects.
//
// Triple buffering: the writer always fills the slot after `latest`, so a slot
// that was just published is not overwritten for at least two frame periods.
// Each slot carries a sequence number (odd while being written).
#pragma once
#include <windows.h>

#define WEBBYLENS_SHARED_MAGIC   0x4E4C4257u   // 'WBLN'
#define WEBBYLENS_SHARED_VERSION 1u
#define WEBBYLENS_SLOT_COUNT     3
#define WEBBYLENS_MAX_PIXELS     (1920u * 1080u)
#define WEBBYLENS_SLOT_BYTES     (WEBBYLENS_MAX_PIXELS * 3u / 2u)   // NV12
#define WEBBYLENS_HEADER_BYTES   4096u
#define WEBBYLENS_FILE_BYTES     (WEBBYLENS_HEADER_BYTES + WEBBYLENS_SLOT_COUNT * WEBBYLENS_SLOT_BYTES)
#define WEBBYLENS_STALE_100NS    (10000000LL)                       // 1 s without frames = no signal

#pragma pack(push, 8)
struct WebbyLensSlot
{
    volatile LONG64 seq;       // odd while writing
    UINT32 width;              // NV12, stride == width, UV plane follows Y plane
    UINT32 height;
    LONG64 timestamp;          // MFGetSystemTime() when written
    LONG64 reserved;
};

struct WebbyLensShared
{
    UINT32 magic;
    UINT32 version;
    UINT32 slotBytes;
    UINT32 slotCount;
    volatile LONG latest;      // index of newest complete slot, -1 = none
    UINT32 hostPid;
    volatile LONG64 heartbeat; // MFGetSystemTime() of the last published frame
    WebbyLensSlot slots[WEBBYLENS_SLOT_COUNT];
    volatile LONG paused;      // 1 = user paused WebbyLens: camera shows the "Paused" picture
    LONG reserved0;
    volatile LONG64 consumerBeat; // written by the camera: MFGetSystemTime() while an app receives frames
};
#define WEBBYLENS_CONSUMER_ACTIVE_100NS (30000000LL)   // camera counts as "in use" up to 3 s after the last frame
#pragma pack(pop)

static_assert(sizeof(WebbyLensShared) <= WEBBYLENS_HEADER_BYTES, "header too large");

inline BYTE* WebbyLensSlotData(void* base, int slot)
{
    return static_cast<BYTE*>(base) + WEBBYLENS_HEADER_BYTES + (size_t)slot * WEBBYLENS_SLOT_BYTES;
}

// %ProgramData%\WebbyLens\frames.bin
inline bool WebbyLensSharedPath(wchar_t* path, DWORD cch, bool createDir)
{
    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ProgramData", base, MAX_PATH);
    if (!n || n >= MAX_PATH)
        lstrcpyW(base, L"C:\\ProgramData");
    wchar_t dir[MAX_PATH];
    if (lstrlenW(base) + 10 >= MAX_PATH)
        return false;
    lstrcpyW(dir, base);
    lstrcatW(dir, L"\\WebbyLens");
    if (createDir)
        CreateDirectoryW(dir, nullptr);
    if ((DWORD)lstrlenW(dir) + 12 >= cch)
        return false;
    lstrcpyW(path, dir);
    lstrcatW(path, L"\\frames.bin");
    return true;
}
