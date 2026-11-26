#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <string>
#include <vector>
#include <iostream>
// (v38 D-29b) No 'using namespace std;' in this header: every name
// below is std::-qualified, so including this header never pollutes
// the global namespace of the including file.

struct Image {                       
    int width = 0, height = 0;
    std::vector<unsigned char> pixels;           
    unsigned char at(int x, int y) const 
{ 
  return pixels[y * width + x]; 
}
};

// Encoder 
bool isValidCode(const std::string& digits);               // exactly 6 digits.
std::vector<int> encodeToWidths(const std::string& digits);// alternating bar/space widths in modules.
Image renderBarcode(const std::string& digits, int module = 3, int height = 120);

// Image I/O 
bool saveBMP(const Image& img, const std::string& path);
bool loadBMP(const std::string& path, Image& out);         // converts to grayscale.

// Decoder
bool decodeBarcode(const Image& img, std::string& digitsOut);

// Camera 
bool captureFrame(Image& out);
