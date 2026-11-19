#pragma once
#include "common/common.h"

// IFrameSource - the contract for anything that can hand the app a
// camera frame (OCP / AoC). Scanning code (services/scan_service)
// depends on this interface, not on the Windows camera code, so the
// frame source can be exchanged (another camera API, a test source)
// without modifying the callers. The real implementation is
// CameraFrameSource (camera/camera.h), built on captureFrame().
class IFrameSource
{
public:
    virtual ~IFrameSource() = default;

    // Capture one frame. True when 'out' holds a new image.
    virtual bool capture(Image& out) = 0;
};
