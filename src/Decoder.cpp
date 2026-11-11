#include "decoder.h"

#include <algorithm>
#include <vector>
#include <string>
#include <cmath>

namespace
{

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

    struct Run
    {
        bool isBlack;
        int length;
    };

    // Classifies 5 element lengths into a digit 0-9 using the ITF 2-of-5 rule
    // (exactly 2 wide and 3 narrow elements).
    char classifyDigit(const int lengths[5])
    {
        int sorted[5];
        for (int i = 0; i < 5; ++i)
        {
            sorted[i] = lengths[i];
        }
        std::sort(sorted, sorted + 5);

        // sorted[0..2] are narrow, sorted[3..4] are wide.
        // Wide elements must be distinctly larger than narrow elements.
        if (sorted[3] <= sorted[2])
        {
            return '\0';
        }

        double threshold = (sorted[2] + sorted[3]) / 2.0;

        std::string pattern = "";
        int wideCount = 0;
        for (int i = 0; i < 5; ++i)
        {
            if (lengths[i] > threshold)
            {
                pattern += 'W';
                wideCount++;
            }
            else
            {
                pattern += 'N';
            }
        }

        if (wideCount != 2)
        {
            return '\0';
        }

        for (int d = 0; d < 10; ++d)
        {
            if (pattern == kPatterns[d])
            {
                return static_cast<char>('0' + d);
            }
        }

        return '\0';
    }

    // Attempts to decode 37 consecutive alternating runs [B, W, B, W, ...]
    bool tryDecode37Runs(const int runs[37], std::string& outDigits)
    {
        // Start pattern: runs[0..3] (all narrow)
        double narrowEst = (runs[0] + runs[1] + runs[2] + runs[3]) / 4.0;
        if (narrowEst < 0.75)
        {
            return false;
        }

        // Stop pattern: runs[34] is wide, runs[35] and runs[36] are narrow
        if (runs[34] < narrowEst * 1.3)
        {
            return false;
        }

        std::string result = "";

        // 3 pairs: each pair contains 5 bars and 5 spaces interleaved
        for (int p = 0; p < 3; ++p)
        {
            int barLengths[5];
            int spaceLengths[5];

            for (int k = 0; k < 5; ++k)
            {
                barLengths[k] = runs[4 + p * 10 + k * 2];
                spaceLengths[k] = runs[4 + p * 10 + k * 2 + 1];
            }

            char dBar = classifyDigit(barLengths);
            char dSpace = classifyDigit(spaceLengths);

            if (dBar == '\0' || dSpace == '\0')
            {
                return false;
            }

            result += dBar;
            result += dSpace;
        }

        // Project requirement: exactly 6 digits, first digit non-zero
        if (result.length() == 6 && result[0] != '0')
        {
            outDigits = result;
            return true;
        }

        return false;
    }

    // Binarizes a 1D scanline using a specified threshold and extracts alternating runs
    void extractRuns(const std::vector<unsigned char>& line, int threshold, std::vector<Run>& outRuns)
    {
        outRuns.clear();
        if (line.empty())
        {
            return;
        }

        bool curIsBlack = (line[0] < threshold);
        int curLen = 0;

        for (size_t i = 0; i < line.size(); ++i)
        {
            bool isBlack = (line[i] < threshold);
            if (isBlack == curIsBlack)
            {
                curLen++;
            }
            else
            {
                outRuns.push_back({ curIsBlack, curLen });
                curIsBlack = isBlack;
                curLen = 1;
            }
        }

        if (curLen > 0)
        {
            outRuns.push_back({ curIsBlack, curLen });
        }
    }

    // Tries to decode a 1D grayscale line across multiple thresholding strategies
    bool decodeScanline(const std::vector<unsigned char>& line, std::string& outDigits)
    {
        if (line.size() < 37)
        {
            return false;
        }

        int minVal = 255;
        int maxVal = 0;
        long long sum = 0;
        for (size_t i = 0; i < line.size(); ++i)
        {
            int v = line[i];
            if (v < minVal)
            {
                minVal = v;
            }
            if (v > maxVal)
            {
                maxVal = v;
            }
            sum += v;
        }

        // Insufficient contrast
        if (maxVal - minVal < 25)
        {
            return false;
        }

        int midThresh = (minVal + maxVal) / 2;
        int avgThresh = static_cast<int>(sum / line.size());
        int lowThresh = (minVal * 2 + maxVal) / 3;
        int highThresh = (minVal + maxVal * 2) / 3;

        int candidateThresholds[4] = { midThresh, avgThresh, lowThresh, highThresh };

        std::vector<Run> runs;
        runs.reserve(line.size());

        for (int tIdx = 0; tIdx < 4; ++tIdx)
        {
            int thresh = candidateThresholds[tIdx];
            extractRuns(line, thresh, runs);

            if (runs.size() < 37)
            {
                continue;
            }

            // Look for any slice of 37 consecutive alternating runs starting with Black
            for (size_t i = 0; i + 37 <= runs.size(); ++i)
            {
                if (!runs[i].isBlack)
                {
                    continue;
                }

                bool alternating = true;
                for (size_t k = 0; k < 37; ++k)
                {
                    bool expectedBlack = (k % 2 == 0);
                    if (runs[i + k].isBlack != expectedBlack)
                    {
                        alternating = false;
                        break;
                    }
                }

                if (!alternating)
                {
                    continue;
                }

                int forwardRuns[37];
                int reverseRuns[37];
                for (int k = 0; k < 37; ++k)
                {
                    forwardRuns[k] = runs[i + k].length;
                    reverseRuns[k] = runs[i + 36 - k].length;
                }

                // Forward scan
                if (tryDecode37Runs(forwardRuns, outDigits))
                {
                    return true;
                }

                // Reversed scan (for upside-down 180-deg barcodes)
                std::string revDigits;
                if (tryDecode37Runs(reverseRuns, revDigits))
                {
                    outDigits = revDigits;
                    return true;
                }
            }
        }

        return false;
    }

} // namespace

bool decodeBarcode(const Image& img, string& digitsOut)
{
    digitsOut.clear();

    if (img.width <= 0 || img.height <= 0 || img.pixels.empty())
    {
        return false;
    }

    int w = img.width;
    int h = img.height;

    // 1. Try horizontal scanlines at various heights (dense sampling across central region)
    std::vector<int> yRatios = {
        50, 45, 55, 40, 60, 35, 65, 30, 70, 25, 75, 20, 80, 15, 85, 10, 90
    };

    std::vector<unsigned char> scanline(w);

    for (size_t r = 0; r < yRatios.size(); ++r)
    {
        int y = (h * yRatios[r]) / 100;
        if (y < 0)
        {
            y = 0;
        }
        if (y >= h)
        {
            y = h - 1;
        }

        for (int x = 0; x < w; ++x)
        {
            scanline[x] = img.pixels[y * w + x];
        }

        if (decodeScanline(scanline, digitsOut))
        {
            return true;
        }

        // Also try right-to-left scanline for upside-down images
        std::vector<unsigned char> rtlScanline(w);
        for (int x = 0; x < w; ++x)
        {
            rtlScanline[x] = scanline[w - 1 - x];
        }

        if (decodeScanline(rtlScanline, digitsOut))
        {
            return true;
        }
    }

    // 2. Try vertical scanlines in case barcode is oriented vertically (90 degrees)
    std::vector<int> xRatios = {
        50, 45, 55, 40, 60, 35, 65, 30, 70
    };

    std::vector<unsigned char> vertLine(h);

    for (size_t r = 0; r < xRatios.size(); ++r)
    {
        int x = (w * xRatios[r]) / 100;
        if (x < 0)
        {
            x = 0;
        }
        if (x >= w)
        {
            x = w - 1;
        }

        for (int y = 0; y < h; ++y)
        {
            vertLine[y] = img.pixels[y * w + x];
        }

        if (decodeScanline(vertLine, digitsOut))
        {
            return true;
        }

        // Also try bottom-to-top
        std::vector<unsigned char> bttLine(h);
        for (int y = 0; y < h; ++y)
        {
            bttLine[y] = vertLine[h - 1 - y];
        }

        if (decodeScanline(bttLine, digitsOut))
        {
            return true;
        }
    }

    // 3. Try diagonal scanlines (for rotated barcodes, e.g. 10-30 degrees)
    std::vector<int> dyOffsets = { -15, 15, -25, 25, -35, 35 };
    for (size_t d = 0; d < dyOffsets.size(); ++d)
    {
        int dy = dyOffsets[d];
        std::vector<unsigned char> diagLine;
        diagLine.reserve(w);

        int startY = h / 2 - dy;
        int endY = h / 2 + dy;

        for (int x = 0; x < w; ++x)
        {
            int y = startY + (endY - startY) * x / (w > 1 ? w - 1 : 1);
            if (y >= 0 && y < h)
            {
                diagLine.push_back(img.pixels[y * w + x]);
            }
        }

        if (decodeScanline(diagLine, digitsOut))
        {
            return true;
        }
    }

    return false;
}