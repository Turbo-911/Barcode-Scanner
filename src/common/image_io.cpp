#include "common/image_io.h"
#include "common/BMP.h"
#include "common/image.h"

namespace imageio
{
bool loadImage(const std::string& path, Image& out)
{
    if (!loadBMP(path, out)) return false;
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
