#pragma once
#include "common/common.h"
#include <string>
#include <vector>

// IBarcodeEncoder - the contract for turning a 6-digit code into a
// barcode (OCP / AoC). Code that needs barcodes generated depends on
// this interface, not on the ITF implementation, so a different
// symbology could be added later as a new class without modifying
// the callers. The ITF implementation is ItfBarcodeEncoder
// (barcode/encoder.h), built on the original free functions.
class IBarcodeEncoder
{
public:
    virtual ~IBarcodeEncoder() = default;

    // Exactly 6 digits, first digit non-zero.
    virtual bool isValid(const std::string& digits) const = 0;
    // Alternating bar/space widths in modules (empty for invalid codes).
    virtual std::vector<int> encodeToWidths(const std::string& digits) const = 0;
    // The rendered barcode image.
    virtual Image render(const std::string& digits, int module = 3, int height = 120) const = 0;
};
