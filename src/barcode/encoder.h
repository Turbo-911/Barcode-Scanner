#pragma once

#include "common/common.h"
#include "barcode/i_barcode_encoder.h"

bool isValidCode(const std::string& digits);
std::vector<int> encodeToWidths(const std::string& digits);
Image renderBarcode(const std::string& digits, int module, int height);  // defaults live in common.h

// ItfBarcodeEncoder - the IBarcodeEncoder implementation for
// Interleaved 2 of 5. It simply forwards to the free functions above,
// so interface-based callers and function-based callers always share
// the one implementation (reusability).
class ItfBarcodeEncoder : public IBarcodeEncoder
{
public:
    bool isValid(const std::string& digits) const override
    {
        return isValidCode(digits);
    }
    std::vector<int> encodeToWidths(const std::string& digits) const override
    {
        return ::encodeToWidths(digits);
    }
    Image render(const std::string& digits, int module = 3, int height = 120) const override
    {
        return renderBarcode(digits, module, height);
    }
};
