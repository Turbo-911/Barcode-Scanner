#pragma once

#include "Common.h"

// Decodes a 6-digit Interleaved 2 of 5 (ITF) barcode from a grayscale image.
// Returns true and writes to digitsOut on success; returns false if no barcode is found.
bool decodeBarcode(const Image& img, string& digitsOut);#pragma once
