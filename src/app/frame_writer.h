#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "h264_decoder.h"
#include "../common/shared_frame.h"
#include "../common/resample.h"

class FrameWriter
{
public:
    ~FrameWriter() { Close(); }
    bool Open(std::string* error);
    void Write(const Nv12Frame& f);
    void SetNoSignal();   // camera shows "no signal" until the next Write
    void SetPaused(bool paused);   // camera shows the "Paused" picture
    bool ConsumerActive() const;   // an app is currently receiving frames from the camera
    void Close();

private:
    void GrantCameraServiceWrite();
    HANDLE _file = INVALID_HANDLE_VALUE;
    HANDLE _mapping = nullptr;
    void* _view = nullptr;
    WebbyLensShared* _hdr = nullptr;
    std::vector<BYTE> _scaled;
    bool _scaledNotice = false;
    Resampler _rsY, _rsUV;
};
