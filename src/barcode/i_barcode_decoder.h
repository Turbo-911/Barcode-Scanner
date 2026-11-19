#pragma once
#include "common/common.h"
#include <string>

// IBarcodeDecoder - the contract for reading a barcode back into its
// 6-digit code (OCP / AoC). Scanning code (services/scan_service)
// depends on this interface, not on the ITF implementation, so the
// decoder can be exchanged or faked in tests without touching the
// scanning logic. The ITF implementation is ItfBarcodeDecoder
// (barcode/decoder.h), built on the original free function.
class IBarcodeDecoder
{
public:
    virtual ~IBarcodeDecoder() = default;

    // True and digitsOut set when a barcode is found in the image.
    virtual bool decode(const Image& img, std::string& digitsOut) const = 0;
};
