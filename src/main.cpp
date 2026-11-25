#include "App.h"
#include "common/common.h"
#include "ui/BarcodeStudioGui.h"

#include "common/image_io.h"

#include <array>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>

using namespace std;

// Usage:
//   barcode_studio_gui              -> SFML GUI (default)
//   barcode_studio_gui --console    -> original Phase 1 console menu (App)
//   barcode_studio_gui --selftest   -> headless encode->decode check, no window
//   barcode_studio_gui --screenshot <file.png>  -> save one GUI frame (QA)
namespace
{
// D-13: locate the tests/ dir: the CWD (project root) first, then the
// exe's own folder and its parent, so --selftest also works when run
// from a build folder. Empty when tests/ cannot be found.
std::string findTestsDir(const char* argv0)
{
    const char* candidates[] = {"tests", "../tests"};
    for (const char* c : candidates)
    {
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::path(c) / "test_normal.bmp", ec)) return c;
    }
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::canonical(argv0, ec);
    if (!ec)
    {
        const std::filesystem::path bases[] = {exe.parent_path(), exe.parent_path().parent_path()};
        for (const std::filesystem::path& base : bases)
        {
            const std::filesystem::path t = base / "tests";
            std::error_code ec2;
            if (std::filesystem::exists(t / "test_normal.bmp", ec2)) return t.string();
        }
    }
    return "";
}

int runSelfTest(const char* argv0)
{
    const array<string, 6> samples = {"123456", "987654", "100000", "200001", "145555", "999999"};
    int passed = 0;
    int total = 0;
    for (const string& code : samples)
    {
        ++total;
        string result;
        const Image img = renderBarcode(code, 3, 120);
        const bool ok = decodeBarcode(img, result) && result == code;
        cout << code << " -> " << (result.empty() ? "(nothing)" : result)
             << (ok ? "  PASS" : "  FAIL") << "\n";
        if (ok) ++passed;
    }

    // D-13: decode the actual tests/*.bmp stress files, not just
    // in-memory renders. Reported gracefully when tests/ is missing.
    const std::string testsDir = findTestsDir(argv0);
    if (testsDir.empty())
    {
        cout << "tests/ not found - file checks skipped. Run from the project root to include them.\n";
    }
    else
    {
        const std::pair<const char*, const char*> files[] = {
            {"test_normal.bmp", "123456"},
            {"test_rotated.bmp", "987654"},
            {"test_inverted.bmp", "123456"},
            {"test_blurry.bmp", "555555"},
            {"test_noisy.bmp", "888888"},
            {"test_small.bmp", "314159"},
            {"test_large.bmp", "654321"}};
        cout << "Decoding stress files in " << testsDir << "/:\n";
        for (const auto& f : files)
        {
            ++total;
            Image img;
            string result;
            const bool ok = imageio::loadImage(testsDir + "/" + f.first, img) &&
                decodeBarcode(img, result) && result == f.second;
            cout << "  " << f.first << " -> " << (result.empty() ? "(nothing)" : result)
                 << (ok ? "  PASS" : "  FAIL") << "\n";
            if (ok) ++passed;
        }

        // D-37: every generated barcode in barcodes/ must decode back
        // to the code in its own file name.
        const std::filesystem::path root = std::filesystem::path(testsDir).parent_path();
        const std::string barcodesDir = (root / "barcodes").string();
        std::error_code ec;
        if (std::filesystem::is_directory(barcodesDir, ec))
        {
            int bmpPassed = 0;
            int bmpTotal = 0;
            for (const auto& entry : std::filesystem::directory_iterator(barcodesDir, ec))
            {
                const std::string name = entry.path().filename().string();
                if (name.size() != 18 || name.rfind("barcode_", 0) != 0 ||
                    name.compare(14, 4, ".bmp") != 0)
                    continue;
                const std::string code = name.substr(8, 6);
                if (!isValidCode(code)) continue;
                ++bmpTotal;
                ++total;
                Image img;
                string result;
                const bool ok = imageio::loadImage(entry.path().string(), img) &&
                    decodeBarcode(img, result) && result == code;
                if (!ok)
                    cout << "  " << name << " -> "
                         << (result.empty() ? "(nothing)" : result) << "  FAIL\n";
                if (ok) { ++bmpPassed; ++passed; }
            }
            cout << "Product barcodes in " << barcodesDir << "/: "
                 << bmpPassed << "/" << bmpTotal << " decode back.\n";
        }
    }
    cout << "Result: " << passed << "/" << total << " passed.\n";
    return passed == total ? 0 : 1;
}
}

int main(int argc, char** argv)
{
    try
    {
        string mode = argc > 1 ? argv[1] : "";
        if (mode == "--console")
        {
            App app;
            return app.run();
        }
        if (mode == "--selftest") return runSelfTest(argv[0]);
        if (mode == "--screenshot")
        {
            if (argc < 3)
            {
                cerr << "Usage: --screenshot <file.png> [view]\n";
                return 1;
            }
            // D-32: --screenshot must never mutate the real data.
            // Resolve the output path BEFORE changing directory, then
            // copy Data/, barcodes/ and assets/ into a fresh temp
            // folder and run the GUI from there, so the seeded
            // checkouts the QA hooks perform land in the copy.
            std::error_code ec;
            const std::filesystem::path outPath =
                std::filesystem::absolute(argv[2], ec);
            if (ec)
            {
                cerr << "Cannot resolve the screenshot path '" << argv[2] << "'.\n";
                return 1;
            }
            const std::filesystem::path tempBase =
                std::filesystem::temp_directory_path(ec);
            if (ec)
            {
                cerr << "Cannot find the temp directory.\n";
                return 1;
            }
            std::filesystem::path shotDir = tempBase / "barcode_shot";
            for (int suffix = 1; std::filesystem::exists(shotDir, ec); ++suffix)
                shotDir = tempBase / ("barcode_shot_" + std::to_string(suffix));
            std::filesystem::create_directories(shotDir, ec);
            if (ec)
            {
                cerr << "Cannot create the screenshot scratch folder.\n";
                return 1;
            }
            for (const char* dir : {"Data", "barcodes", "assets"})
            {
                const std::filesystem::path src(dir);
                std::error_code copyEc;
                if (std::filesystem::exists(src, copyEc))
                    std::filesystem::copy(src, shotDir / dir,
                        std::filesystem::copy_options::recursive, copyEc);
            }
            std::filesystem::create_directories(shotDir / "bills", ec);
            std::filesystem::current_path(shotDir, ec);
            if (ec)
            {
                cerr << "Cannot change to the screenshot scratch folder.\n";
                return 1;
            }
            std::cout << "Screenshot scratch folder: " << shotDir.string()
                      << " (real Data/ untouched)\n";
            BarcodeStudioGui gui;
            const int view = argc > 3 ? std::stoi(argv[3]) : -1;
            return gui.screenshot(outPath.string(), view) ? 0 : 1;
        }
        BarcodeStudioGui gui;
        return gui.run();
    }
    catch (const std::exception& e)
    {
        cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
    catch (...)
    {
        cerr << "Fatal error: unknown exception.\n";
        return 1;
    }
}
