#include "BMP.h"
#include <fstream>
#include <cstdint>
#include <vector>
using namespace std;
uint16_t readUInt16(ifstream& file)  // Read 2 bytes
{
    unsigned char bytes[2];
    file.read((char*)bytes, 2);
    return bytes[0] | (bytes[1] << 8);
}
uint32_t readUInt32(ifstream& file)   // Read 4 bytes
{
    unsigned char bytes[4];
    file.read((char*)bytes, 4);
    return (uint32_t)bytes[0]| ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}
void writeUInt16(ofstream& file, uint16_t value)  // Write 2 bytes
{
    file.put(value & 255);
    file.put((value >> 8) & 255);
}
void writeUInt32(ofstream& file, uint32_t value)   // Write 4 bytes
{
    file.put(value & 255);
    file.put((value >> 8) & 255);
    file.put((value >> 16) & 255);
    file.put((value >> 24) & 255);
}
bool saveBMP(const Image& img, const string& path)  // Save grayscale image as BMP
{
    if (img.width <= 0 || img.height <= 0)
    {
        return false;
    }
    uint64_t totalPixels = (uint64_t)img.width * img.height;
    if (img.pixels.size() != totalPixels)
    {
        return false;
    }
    uint64_t rowSize = ((uint64_t)img.width + 3) / 4 * 4;
    uint64_t imageSize = rowSize * img.height;
    uint64_t fileSize = 1078 + imageSize;

    if (fileSize > UINT32_MAX)
    {
        return false;
    }
    ofstream file(path, ios::binary | ios::trunc);
    if (!file)
    {
        return false;
    }
    // BMP file header
    file.put('B');
    file.put('M');

    writeUInt32(file, (uint32_t)fileSize);
    writeUInt32(file, 0);
    writeUInt32(file, 1078);

    // BMP information header
    writeUInt32(file, 40);
    writeUInt32(file, (uint32_t)img.width);
    writeUInt32(file, (uint32_t)img.height);
    writeUInt16(file, 1);
    writeUInt16(file, 8);
    writeUInt32(file, 0);
    writeUInt32(file, (uint32_t)imageSize);
    writeUInt32(file, 2835);
    writeUInt32(file, 2835);
    writeUInt32(file, 256);
    writeUInt32(file, 0);

    for (int i = 0; i < 256; i++){  // Write grayscale color palette

        file.put(i);
        file.put(i);
        file.put(i);
        file.put(0);
    }
    vector<unsigned char> row(rowSize, 0);       // Write pixel rows

    for (int y = img.height - 1; y >= 0; y--)
    {
        for (int x = 0; x < img.width; x++)
        {
            row[x] = img.pixels[y * img.width + x];
        }

        file.write((char*)row.data(), rowSize);

        if (!file)
        {
            return false;
        }
    }

    return true;
}

bool loadBMP(const string& path, Image& out) // Load BMP and convert it to grayscale
{
    out = Image{};

    ifstream file(path, ios::binary);

    if (!file)
    {
        return false;
    }
    // Check BMP signature
    char signature[2];

    file.read(signature, 2);

    if (!file || signature[0] != 'B' || signature[1] != 'M')
    {
        return false;
    }
    // Read file header
    readUInt32(file);
    readUInt16(file);
    readUInt16(file);

    uint32_t pixelOffset = readUInt32(file);
    // Read information header
    uint32_t headerSize = readUInt32(file);

    if (headerSize < 40)
    {
        return false;
    }

    int32_t width = (int32_t)readUInt32(file);
    int32_t height = (int32_t)readUInt32(file);
    uint16_t planes = readUInt16(file);
    uint16_t bitsPerPixel = readUInt16(file);

    uint32_t compression = readUInt32(file);
    readUInt32(file);
    readUInt32(file);
    readUInt32(file);

    uint32_t colorsUsed = readUInt32(file);

    readUInt32(file);
    // Check supported BMP format
    if (width <= 0 || height == 0 || height == INT32_MIN)
    {
        return false;
    }

    if (planes != 1 || compression != 0)
    {
        return false;
    }

    if (bitsPerPixel != 8 && bitsPerPixel != 24)
    {
        return false;
    }

    int actualHeight = height < 0 ? -height : height;

    uint64_t totalPixels = (uint64_t)width * actualHeight;

    if (totalPixels > out.pixels.max_size())
    {
        return false;
    }
    // Skip extra information header bytes
    if (headerSize > 40)
    {
        file.seekg(14 + headerSize, ios::beg);
    }
    // Read grayscale palette for 8-bit BMP
    unsigned char red[256];
    unsigned char green[256];
    unsigned char blue[256];

    int paletteSize = 0;

    if (bitsPerPixel == 8)
    {
        paletteSize = colorsUsed == 0 ? 256 : colorsUsed;

        if (paletteSize > 256 || 14ULL + headerSize + paletteSize * 4ULL > pixelOffset)
        {
            return false;
        }

        for (int i = 0; i < paletteSize; i++)
        {
            blue[i] = file.get();
            green[i] = file.get();
            red[i] = file.get();

            file.get();

            if (!file)
            {
                return false;
            }
        }
    }
    // Move to pixel data
    file.seekg(pixelOffset, ios::beg);

    if (!file)
    {
        return false;
    }

    uint64_t rowSize = ((uint64_t)width * bitsPerPixel + 31) / 32 * 4;
    vector<unsigned char> row(rowSize);

    out.width = width;
    out.height = actualHeight;
    out.pixels.resize((size_t)totalPixels);

    // Read pixel rows
    for (int y = 0; y < actualHeight; y++)
    {
        file.read((char*)row.data(), rowSize);

        if (!file)
        {
            out = Image{};
            return false;
        }
        int targetY = height > 0 ? actualHeight - 1 - y : y;
        for (int x = 0; x < width; x++)
        {
            unsigned char gray;

            if (bitsPerPixel == 8)
            {
                int index = row[x];

                if (index >= paletteSize)
                {
                    out = Image{};
                    return false;
                }

                gray = (red[index] + green[index] + blue[index]) / 3;
            }
            else
            {
                int index = x * 3;

                unsigned char b = row[index];
                unsigned char g = row[index + 1];
                unsigned char r = row[index + 2];

                gray = (r + g + b) / 3;
            }

            out.pixels[targetY * width + x] = gray;
        }
    }

    return true;
}
