#include "Image.h"
bool createImage(Image& img, int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    img.width = width;
    img.height = height;

    img.pixels.resize(width * height, 255);

    return true;
}
bool isValidImage(const Image& img)
{
    if (img.width <= 0 || img.height <= 0)
    {
        return false;
    }

    if (img.pixels.size() != (size_t)img.width * img.height)
    {
        return false;
    }

    return true;
}
void clearImage(Image& img)
{
    img.width = 0;
    img.height = 0;
    img.pixels.clear();
}