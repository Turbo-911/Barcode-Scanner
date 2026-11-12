#include "App.h"
#include "Common.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <thread>
#endif

namespace {

const int kModule = 3;       // pixels per narrow bar.
const int kBarHeight = 120;  // barcode image height in pixels.
const int kCameraTries = 30; // how many frames to try before giving up.

// Some input helpers.
std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    std::size_t start = s.find_first_not_of(ws);
    if (start == std::string::npos) return "";
    std::size_t end = s.find_last_not_of(ws);
    return s.substr(start, end - start + 1);
}

// Dragging a file into the console.
std::string stripQuotes(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return s;
}

std::string resolveImagePath(const std::string& path) {
    std::ifstream direct(path.c_str(), std::ios::binary);
    if (direct.good()) return path;

    if (path.find('\\') == std::string::npos &&
        path.find('/') == std::string::npos) {
        const std::string testPath = "tests\\" + path;
        std::ifstream testFile(testPath.c_str(), std::ios::binary);
        if (testFile.good()) return testPath;
    }

    return path;
}

// Prints a prompt and reads one line. Returns false if EOF.
bool ask(const std::string& prompt, std::string& out) {
    std::cout << prompt;
    if (!std::getline(std::cin, out)) return false;
    out = trim(out);
    return true;
}

// Some output helpers.
// Draws one row of the barcode in the console so the user sees it immediately.
void printPreview(const Image& img) {
    if (img.width <= 0 || img.height <= 0) return;

    int y = img.height / 2;
    std::string line;
    for (int x = 0; x < img.width; x += kModule) {
        int sampleX = x + kModule / 2;
        if (sampleX >= img.width) sampleX = img.width - 1;
        line += (img.at(sampleX, y) < 128) ? '#' : ' ';
    }

    std::cout << "\n";
    for (int i = 0; i < 6; ++i) std::cout << "  " << line << "\n";
    std::cout << "\n";
}

void printResult(const std::string& digits) {
    std::cout << "\n  >>> Decoded number: " << digits << "\n\n";
}

} 

// The App.
void App::showMenu() const {
    std::cout << "\n==============================\n"
              << "   BARCODE SCANNER / READER\n"
              << "==============================\n"
              << " 1. Generate barcode from 6-digit number\n"
              << " 2. Read barcode from image file (.bmp)\n"
              << " 3. Scan barcode with camera\n"
              << " 4. Run self-test (encode -> decode)\n"
              << " 0. Exit\n"
              << "------------------------------\n";
}

void App::generateBarcode() {
    std::string digits;
    while (true) {
        if (!ask("Enter a 6-digit number (or 'b' to go back): ", digits)) return;
        if (digits == "b" || digits == "B") return;
        if (isValidCode(digits)) break;
        std::cout << "  Invalid input. Please enter exactly 6 digits (0-9).\n";
    }

    Image img = renderBarcode(digits, kModule, kBarHeight);
    if (img.width <= 0 || img.height <= 0 || img.pixels.empty()) {
        std::cout << "  Error: the encoder did not produce an image.\n";
        return;
    }

    std::cout << "\n  Barcode for " << digits << ":\n";
    printPreview(img);

    std::string defaultName = "barcode_" + digits + ".bmp";
    std::string path;
    if (!ask("Save as (press Enter for \"" + defaultName + "\"): ", path)) return;
    path = stripQuotes(path);
    if (path.empty()) path = defaultName;

    if (saveBMP(img, path))
        std::cout << "  Saved to: " << path << "\n";
    else
        std::cout << "  Error: could not save the file '" << path << "'.\n";
}

void App::readFromFile() {
    std::string path;
    if (!ask("Path to barcode image (.bmp), or 'b' to go back: ", path)) return;
    path = stripQuotes(path);
    if (path.empty() || path == "b" || path == "B") return;
    path = resolveImagePath(path);

    Image img;
    if (!loadBMP(path, img)) {
        std::cout << "  Error: could not open '" << path
                  << "'. Check the path and that it is a valid BMP file.\n";
        return;
    }
    std::cout << "  Loaded image: " << img.width << " x " << img.height << "\n";

    std::string digits;
    if (decodeBarcode(img, digits))
        printResult(digits);
    else
        std::cout << "\n  No barcode found in this image.\n";
}

void App::scanWithCamera() {
    std::cout << "Starting camera... point a barcode at it (up to "
              << kCameraTries << " attempts).\n";

    std::map<std::string, int> recentDetections;
    std::vector<std::string> detectionWindow;
    for (int attempt = 1; attempt <= kCameraTries; ++attempt) {
        Image frame;
        if (!captureFrame(frame)) {
            std::cout << "  Error: camera capture failed. See the [Camera] "
                         "diagnostic line above for the failing stage and HRESULT.\n";
            return;
        }

        std::string digits;
        if (decodeBarcode(frame, digits)) {
            detectionWindow.push_back(digits);
            ++recentDetections[digits];
            if (detectionWindow.size() > 5) {
                std::map<std::string, int>::iterator old =
                    recentDetections.find(detectionWindow.front());
                if (old != recentDetections.end() && old->second > 0) {
                    --old->second;
                }
                detectionWindow.erase(detectionWindow.begin());
            }

            if (recentDetections[digits] >= 3) {
                printResult(digits);
                return;
            }
        }

        // Keeping the last frame so we can debug why decoding failed.
        if (attempt == kCameraTries) saveBMP(frame, "last_capture.bmp");

        std::cout << "  Attempt " << attempt << "/" << kCameraTries
                  << ": no barcode found, trying again...\r" << std::flush;
#ifdef _WIN32
        Sleep(150);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
#endif
    }

    std::cout << "\n  Could not read a barcode from the camera. "
                 "Last frame saved as last_capture.bmp\n";
}

void App::selfTest() {
    const std::vector<std::string> samples = {
        "123456", "987654", "100000", "200001", "555555", "999999"};

    int passed = 0;
    std::cout << "\nRunning encode -> decode round trip:\n";
    for (const std::string& code : samples) {
        std::string result;
        Image img = renderBarcode(code, kModule, kBarHeight);
        bool ok = decodeBarcode(img, result) && result == code;

        std::cout << "  " << code << " -> "
                  << (result.empty() ? "(nothing)" : result)
                  << (ok ? "   PASS" : "   FAIL") << "\n";
        if (ok) ++passed;
    }
    std::cout << "Result: " << passed << "/" << samples.size() << " passed.\n";
}

int App::run() {
    std::string choice;
    while (true) {
        showMenu();
        if (!ask("Choose an option: ", choice)) return 0;  

        if (choice == "1")      generateBarcode();
        else if (choice == "2") readFromFile();
        else if (choice == "3") scanWithCamera();
        else if (choice == "4") selfTest();
        else if (choice == "0") {
            std::cout << "Goodbye!\n";
            return 0;
        } else {
            std::cout << "  Please enter 0, 1, 2, 3 or 4.\n";
        }
    }
}
