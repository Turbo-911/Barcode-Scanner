#include "common/image_io.h"
#include "common/BMP.h"
#include "common/image.h"

// D-31 (v38): PNG/JPG support via stb_image (single public-domain header,
// vendored as src/common/stb_image.h from upstream nothings/stb). No
// hand-written PNG/JPG decoder: stb decodes to 8-bit greyscale (1
// requested channel) for the decoder. The implementation lives in this
// one translation unit only (STB_IMAGE_IMPLEMENTATION here, nowhere else).
#define STB_IMAGE_IMPLEMENTATION
#include "common/stb_image.h"

#include <algorithm>
#include <cctype>

namespace imageio
{
namespace
{

bool hasExtensionLower(const std::string& path, const char* ext)
{
    const std::string lower = [&path]
    {
        std::string copy = path;
        for (char& ch : copy)
        {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return copy;
    }();
    const std::string suffix(ext);
    return lower.size() >= suffix.size()
        && lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Load a PNG/JPG/JPEG file as 8-bit greyscale through stb_image.
bool loadViaStb(const std::string& path, Image& out)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 1);
    if (data == nullptr || width <= 0 || height <= 0)
    {
        if (data != nullptr)
        {
            stbi_image_free(data);
        }
        return false;
    }
    out.width = width;
    out.height = height;
    out.pixels.assign(data, data + static_cast<size_t>(width) * static_cast<size_t>(height));
    stbi_image_free(data);
    return true;
}

}  // namespace

bool loadImage(const std::string& path, Image& out)
{
    bool loaded = false;
    if (hasExtensionLower(path, ".png") || hasExtensionLower(path, ".jpg")
        || hasExtensionLower(path, ".jpeg"))
    {
        loaded = loadViaStb(path, out);
    }
    else
    {
        loaded = loadBMP(path, out);
    }
    if (!loaded) return false;
    if (!isValidImage(out))
    {
        out = Image{};
        return false;
    }
    return true;
}

bool saveImage(const Image& img, const std::string& path)
{
    if (!isValidImage(img)) return false;
    return saveBMP(img, path);
}
}
