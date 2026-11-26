#include "App.h"
#include "common/common.h"
#include "common/image_io.h"
#include "barcode/decoder.h"
#include "camera/camera.h"
#include "services/scan_service.h"

#include "persistence/i_bill_detail_repo.h"
#include "persistence/i_bill_repo.h"
#include "persistence/i_category_repo.h"
#include "persistence/i_order_repo.h"
#include "persistence/i_product_repo.h"
#include "persistence/i_seasons_repo.h"
#include "services/category_service.h"
#include "services/order_service.h"
#include "services/product_service.h"
#include "services/seasons_service.h"

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
              << " 2. Read barcode from image file (.bmp/.png/.jpg)\n"
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

    if (imageio::saveImage(img, path))
        std::cout << "  Saved to: " << path << "\n";
    else
        std::cout << "  Error: could not save the file '" << path << "'.\n";
}

void App::readFromFile() {
    std::string path;
    if (!ask("Path to barcode image (.bmp/.png/.jpg), or 'b' to go back: ", path)) return;
    path = stripQuotes(path);
    if (path.empty() || path == "b" || path == "B") return;
    path = resolveImagePath(path);

    Image img;
    if (!imageio::loadImage(path, img)) {
        std::cout << "  Error: could not open '" << path
                  << "'. Check the path and that it is a valid image file (BMP/PNG/JPG).\n";
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

    // The scan pipeline is interface-based (OCP): frames come from an
    // IFrameSource, decoding is an IBarcodeDecoder, and ScanService
    // owns the "same code 3 times within the last 5 decodes" rule
    // that decides when a scan is trusted.
    CameraFrameSource camera;
    ItfBarcodeDecoder decoder;
    ScanService scanner(decoder);

    // D-12: a failed capture no longer aborts the scan on the spot -
    // one dropped frame used to end all 30 tries. Captures are
    // retried like decode failures, up to kCameraTries attempts,
    // and the whole scan stops at an overall 45-second deadline
    // (checked between attempts; a single blocking capture call
    // itself cannot be interrupted from here).
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(45);
    int captureFailures = 0;
    Image lastFrame;
    bool haveLastFrame = false;

    for (int attempt = 1; attempt <= kCameraTries; ++attempt) {
        if (std::chrono::steady_clock::now() > deadline) {
            std::cout << "\n  Stopped: the 45-second camera scan deadline was reached.\n";
            break;
        }

        Image frame;
        if (!camera.capture(frame)) {
            ++captureFailures;
            std::cout << "  Attempt " << attempt << "/" << kCameraTries
                      << ": camera capture failed (" << captureFailures
                      << " in a row), retrying...\n";
#ifdef _WIN32
            Sleep(150);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
#endif
            continue;
        }
        captureFailures = 0;

        // Keeping the last good frame so we can debug why decoding failed.
        lastFrame = frame;
        haveLastFrame = true;

        if (scanner.feedFrame(frame)) {
            printResult(scanner.confirmedCode());
            return;
        }

        std::cout << "  Attempt " << attempt << "/" << kCameraTries
                  << ": no barcode found, trying again...\r" << std::flush;
#ifdef _WIN32
        Sleep(150);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
#endif
    }

    if (haveLastFrame) imageio::saveImage(lastFrame, "last_capture.bmp");
    std::cout << "\n  Could not read a barcode from the camera. ";
    if (haveLastFrame)
        std::cout << "Last frame saved as last_capture.bmp\n";
    else
        std::cout << "No frame was ever captured, so there is nothing to save.\n";
}

// D-13: find the tests/ dir next to the current folder (project root)
// or one level up (a build folder). Empty when it cannot be found.
std::string findTestsDir() {
    const char* candidates[] = {"tests", "../tests"};
    for (const char* c : candidates) {
        std::ifstream probe(std::string(c) + "/test_normal.bmp", std::ios::binary);
        if (probe.good()) return c;
    }
    return "";
}

void App::selfTest() {
    const std::vector<std::string> samples = {
        "123456", "987654", "100000", "200001", "145555", "999999"};

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
    // D-13: also decode the real stress BMPs in tests/, not just
    // in-memory renders. Graceful when tests/ is not next to the CWD.
    const std::string testsDir = findTestsDir();
    int filePassed = 0;
    int fileTotal = 0;
    if (testsDir.empty())
    {
        std::cout << "tests/ not found next to the current folder - file checks skipped.\n"
                     "Run the console from the project root to include them.\n";
    }
    else
    {
        const std::vector<std::pair<std::string, std::string>> files = {
            {"test_normal.bmp", "123456"},
            {"test_rotated.bmp", "987654"},
            {"test_inverted.bmp", "123456"},
            {"test_blurry.bmp", "555555"},
            {"test_noisy.bmp", "888888"},
            {"test_small.bmp", "314159"},
            {"test_large.bmp", "654321"}};
        std::cout << "\nDecoding stress files in " << testsDir << "/:\n";
        for (const auto& f : files)
        {
            ++fileTotal;
            Image img;
            std::string result;
            const bool ok = imageio::loadImage(testsDir + "/" + f.first, img) &&
                decodeBarcode(img, result) && result == f.second;
            std::cout << "  " << f.first << " -> "
                      << (result.empty() ? "(nothing)" : result)
                      << (ok ? "   PASS" : "   FAIL") << "\n";
            if (ok) ++filePassed;
        }
        std::cout << "File checks: " << filePassed << "/" << fileTotal << " passed.\n";
    }
    std::cout << "Result: " << (passed + filePassed) << "/"
              << (samples.size() + static_cast<std::size_t>(fileTotal)) << " passed.\n";
}

int App::run() {
    // v37 (D-02): roll forward any checkout/edit interrupted by a
    // crash, before the menu opens. Same recovery the GUI runs at
    // startup; safe to run twice (idempotent).
    {
        ProductFileRepository productRepo;
        OrderFileRepository orderRepo;
        OrderDetailFileRepository detailRepo;
        BillFileRepository billRepo;
        BillDetailFileRepository billDetailRepo;
        CategoryFileRepository categoryRepo;
        SeasonsFileRepository seasonsRepo;
        CategoryService categories(categoryRepo);
        SeasonsService seasons(seasonsRepo);
        ProductService products(productRepo, categories);
        OrderService orders(products, seasons, orderRepo, detailRepo,
            billRepo, billDetailRepo);
        const RecoveryReport recovery = orders.recoverJournal();
        if (recovery.recovered > 0 || recovery.droppedLines > 0
            || recovery.skipped > 0)
        {
            std::cout << "Startup recovery: recovered "
                      << recovery.recovered << " interrupted checkout(s)";
            if (!recovery.recoveredOrderIds.empty())
            {
                std::cout << " (";
                for (std::size_t i = 0; i < recovery.recoveredOrderIds.size(); ++i)
                {
                    if (i > 0) std::cout << ", ";
                    std::cout << recovery.recoveredOrderIds[i];
                }
                std::cout << ")";
            }
            if (recovery.droppedLines > 0)
                std::cout << "; " << recovery.droppedLines
                          << " journal line(s) ignored";
            if (recovery.skipped > 0)
                std::cout << "; " << recovery.skipped << " still pending";
            std::cout << ".\n";
        }
    }
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
