#include "App.h"
#include "common/common.h"
#include "ui/BarcodeStudioGui.h"

#include <array>
#include <exception>
#include <iostream>
#include <string>

using namespace std;

// Usage:
//   barcode_studio_gui              -> SFML GUI (default)
//   barcode_studio_gui --console    -> original Phase 1 console menu (App)
//   barcode_studio_gui --selftest   -> headless encode->decode check, no window
//   barcode_studio_gui --screenshot <file.png>  -> save one GUI frame (QA)
namespace
{
int runSelfTest()
{
    const array<string, 6> samples = {"123456", "987654", "100000", "200001", "555555", "999999"};
    int passed = 0;
    for (const string& code : samples)
    {
        string result;
        const Image img = renderBarcode(code, 3, 120);
        const bool ok = decodeBarcode(img, result) && result == code;
        cout << code << " -> " << (result.empty() ? "(nothing)" : result)
             << (ok ? "  PASS" : "  FAIL") << "\n";
        if (ok) ++passed;
    }
    cout << "Result: " << passed << "/" << samples.size() << " passed.\n";
    return passed == static_cast<int>(samples.size()) ? 0 : 1;
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
        if (mode == "--selftest") return runSelfTest();
        if (mode == "--screenshot")
        {
            if (argc < 3)
            {
                cerr << "Usage: --screenshot <file.png>\n";
                return 1;
            }
            BarcodeStudioGui gui;
            const int view = argc > 3 ? std::stoi(argv[3]) : -1;
            return gui.screenshot(argv[2], view) ? 0 : 1;
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
