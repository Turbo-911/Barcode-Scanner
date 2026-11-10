#include "Common.h"
#include <iostream>
namespace {

    const int kNarrow = 1;
    const int kWide = 3;             // wide = 3x narrow (README).
    const int kQuietZoneModules = 10;

    // N = narrow, W = wide; exactly two W per digit.
    const char* kPatterns[10] = {
        "NNWWN",  // 0
        "WNNNW",  // 1
        "NWNNW",  // 2
        "WWNNN",  // 3
        "NNWNW",  // 4
        "WNWNN",  // 5
        "NWWNN",  // 6
        "NNNWW",  // 7
        "WNNWN",  // 8
        "NWNWN"   // 9
    };

    int widthOf(char c) { return c == 'W' ? kWide : kNarrow; }

} // namespace

// Exactly 6 digits, and the first digit must not be 0 (project requirement).
bool isValidCode(const string& digits) {
    if (digits.size() != 6) return false;
    for (char c : digits)
        if (c < '0' || c > '9') return false;
    return digits[0] != '0';
}

// Returns 37 widths: start(4) + 3 pairs(10 each) + stop(3).
// Elements alternate bar, space, bar, space... starting with a bar.
vector<int> encodeToWidths(const string& digits) {
    vector<int> widths;
    if (!isValidCode(digits)) return widths;
    widths.reserve(37);

    // Start: narrow bar, narrow space, narrow bar, narrow space.
    for (int i = 0; i < 4; ++i) widths.push_back(kNarrow);

    // Each pair: first digit -> bars, second digit -> spaces.
    for (int p = 0; p < 3; ++p) {
        const char* bars = kPatterns[digits[2 * p] - '0'];
        const char* spaces = kPatterns[digits[2 * p + 1] - '0'];
        for (int i = 0; i < 5; ++i) {
            widths.push_back(widthOf(bars[i]));    // bar
            widths.push_back(widthOf(spaces[i]));  // space
        }
    }

    // Stop: wide bar, narrow space, narrow bar.
    widths.push_back(kWide);
    widths.push_back(kNarrow);
    widths.push_back(kNarrow);
    return widths;
}

Image renderBarcode(const string& digits, int module, int height) {
    Image img;  // empty image means failure (App checks for this).
    vector<int> widths = encodeToWidths(digits);
    if (widths.empty() || module <= 0 || height <= 0) return img;

    int totalModules = 2 * kQuietZoneModules;
    for (int w : widths) totalModules += w;

    img.width = totalModules * module;
    img.height = height;
    img.pixels.assign(static_cast<size_t>(img.width) * img.height, 255);  // all white

    int x = kQuietZoneModules * module;  // skip the left quiet zone
    bool isBar = true;                   // first element is a bar
    for (int w : widths) {
        int px = w * module;
        if (isBar) {
            for (int y = 0; y < img.height; ++y)
                for (int i = 0; i < px; ++i)
                    img.pixels[static_cast<size_t>(y) * img.width + x + i] = 0;
        }
        x += px;
        isBar = !isBar;
    }
    return img;
}