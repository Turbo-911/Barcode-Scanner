#pragma once
#include "common/common.h"
#include <string>

// image_io - the application-level image file API (SRP): loading an
// image from disk ready for decoding, and saving an image back.
// It validates the image on top of the raw BMP codec (BMP.h), so
// callers get one checked entry point instead of repeating the
// load + validity checks at every call site.
namespace imageio
{
    // Load an image file and confirm it holds a usable image.
    // BMP goes through the project BMP codec; PNG/JPG/JPEG go
    // through stb_image, always converted to 8-bit greyscale
    // for the decoder (D-31, v38). Extension match is case-insensitive.
    bool loadImage(const std::string& path, Image& out);
    // Save an image as a BMP file (false for an empty/invalid image).
    bool saveImage(const Image& img, const std::string& path);
}
