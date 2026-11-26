#pragma once

#include "common/common.h"
#include "barcode/i_barcode_decoder.h"

// Decodes a 6-digit Interleaved 2 of 5 (ITF) barcode from a grayscale image.
// Returns true and writes to digitsOut on success; returns false if no barcode is found.
bool decodeBarcode(const Image& img, std::string& digitsOut);

// ItfBarcodeDecoder - the IBarcodeDecoder implementation for
// Interleaved 2 of 5. It simply forwards to decodeBarcode() above,
// so interface-based callers (e.g. ScanService) and function-based
// callers always share the one implementation (reusability).
class ItfBarcodeDecoder : public IBarcodeDecoder
{
public:
    bool decode(const Image& img, std::string& digitsOut) const override
    {
        return decodeBarcode(img, digitsOut);
    }
};
