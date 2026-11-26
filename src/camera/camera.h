#pragma once

#include "common/common.h"
#include "camera/i_frame_source.h"
#include <string>
#include <vector>

struct IMFSourceReader;

class Camera
{
public:
    Camera();
    ~Camera();

    bool initialize();
    // v38: open a specific device by index (0 = first). The
    // no-arg initialize() keeps working and opens device 0.
    // If the camera is already initialised, either form returns
    // true WITHOUT switching devices - call cleanup() first to
    // move to another device. A bad index fails (false) instead
    // of silently opening device 0; a busy requested device
    // falls back to the next enumerated device once (D-12).
    bool initialize(int deviceIndex);
    // v38: friendly names of the video capture devices, in index
    // order (entry [i] is the name of device i). Media Foundation
    // path: the real MF friendly names. VFW / other fallback:
    // whatever the backend can enumerate, at minimum "Camera 0".
    // Empty when enumeration itself fails (MF path only).
    static std::vector<std::string> enumerateDeviceNames();
    void cleanup();
    bool isInitialized() const;

    bool captureFrame(Image& out);
    bool captureFrame(Image* out);
    unsigned char* captureRawGrayscale(int& width, int& height);

    static bool captureSingleFrame(Image& out);

    void onVideoFrame(const unsigned char* data, int width, int height, int stride);

private:
    unsigned char* readFrameRaw(int& outWidth, int& outHeight);

    IMFSourceReader* m_pSourceReader;
    int m_width;
    int m_height;
    long m_stride;
    unsigned long m_formatFourCC;
    bool m_isInitialized;
    bool m_comInitialized;
    bool m_mfStarted;
    void* m_captureWindow;
    std::vector<unsigned char> m_latestFrame;
    int m_frameWidth;
    int m_frameHeight;
    bool m_frameReady;
};

bool captureFrame(Image& out);

// CameraFrameSource - the IFrameSource implementation for the real
// camera. It forwards to the captureFrame() free function above, so
// interface-based callers (e.g. the console scan flow through
// ScanService) and function-based callers share one implementation.
class CameraFrameSource : public IFrameSource
{
public:
    bool capture(Image& out) override { return captureFrame(out); }
};
