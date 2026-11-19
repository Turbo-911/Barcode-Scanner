#pragma once
#include "barcode/i_barcode_decoder.h"
#include "common/common.h"
#include <map>
#include <string>
#include <vector>

// ScanService - the "is this scan trustworthy?" logic of camera
// scanning (SRP): a code is only accepted after it has been decoded
// consistently - the same code kRequired times within the last
// kWindowSize successful decodes. This is exactly the rule the
// console app always used; it now lives in one reusable service
// instead of inside App's menu code.
//
// It works through IBarcodeDecoder, so the decoding algorithm can
// be exchanged without touching this class (OCP). Feed it frames
// from any IFrameSource; it never touches the camera itself.
class ScanService
{
public:
    static constexpr int kWindowSize = 5;  // successful decodes remembered
    static constexpr int kRequired = 3;    // matching decodes needed to accept

    explicit ScanService(IBarcodeDecoder& decoder) : m_decoder(decoder) {}

    void reset();

    // Decode one frame and update the consistency window.
    // Returns true on the frame where a code becomes confirmed.
    bool feedFrame(const Image& frame);

    bool confirmed() const { return !m_confirmed.empty(); }
    const std::string& confirmedCode() const { return m_confirmed; }

private:
    IBarcodeDecoder& m_decoder;
    std::map<std::string, int> m_recentDetections;
    std::vector<std::string> m_detectionWindow;
    std::string m_confirmed;
};
