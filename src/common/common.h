#pragma once
#include <string>
#include <vector>
#include <iostream>
using namespace std;

struct Image {                       
    int width = 0, height = 0;
    vector<unsigned char> pixels;           
    unsigned char at(int x, int y) const 
{ 
  return pixels[y * width + x]; 
}
};

// Encoder 
bool isValidCode(const string& digits);               // exactly 6 digits.
vector<int> encodeToWidths(const string& digits);// alternating bar/space widths in modules.
Image renderBarcode(const string& digits, int module = 3, int height = 120);

// Image I/O 
bool saveBMP(const Image& img, const string& path);
bool loadBMP(const string& path, Image& out);         // converts to grayscale.

// Decoder
bool decodeBarcode(const Image& img, string& digitsOut);

// Camera 
bool captureFrame(Image& out);
