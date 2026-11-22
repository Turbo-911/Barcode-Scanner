#include "ui/BarcodeStudioGui.h"
#include "ui/Theme.h"
#include "persistence/order_serialiser.h"
#include "persistence/record_file.h"

#include "common/BMP.h"
#include "camera/camera.h"
#include "common/string_utils.h"
#include "domain/enums.h"
#include "barcode/decoder.h"
#include "services/pdf_bill.h"
#include "common/date_time.h"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <random>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

// ============================================================================
//  Chili Mili - Barcode POS Studio (Phase 1 + Phase 2).
//  Same Phase 1 core functions, untouched. Everything new is presentation:
//  a mascot whose eyes follow your mouse, floating hearts, mouse sparkle
//  trails, celebration bursts, sliding screen transitions, pop-in digit
//  tiles, an animated barcode reveal and a staggered self-test.
// ============================================================================
namespace
{
    const sf::Vector2u kWindowSize(1000u, 680u);
    const float kLeft = ui::theme::SidebarWidth + 26.0f;  // 246
    std::mt19937& rng()
    {
        static std::mt19937 r{std::random_device{}()};
        return r;
    }
    float frand(float lo, float hi)
    {
        return std::uniform_real_distribution<float>(lo, hi)(rng());
    }
    float easeOut(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }

    bool loadFont(sf::Font& font)
    {
        // Times New Roman everywhere (user's choice): the real font on
        // Windows first, then the bundled metric-compatible serif.
        const char* candidates[] = {
            "C:/Windows/Fonts/times.ttf",
            "assets/fonts/TimesNewRoman.ttf",
            "../assets/fonts/TimesNewRoman.ttf",
            "Barcode-Scanner/assets/fonts/TimesNewRoman.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf",
            "assets/fonts/NotoSans-Regular.ttf",
            "../assets/fonts/NotoSans-Regular.ttf",
            "Barcode-Scanner/assets/fonts/NotoSans-Regular.ttf",
            "C:/Windows/Fonts/segoeui.ttf",
            "C:/Windows/Fonts/arial.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        };
        for (const char* path : candidates)
            if (font.openFromFile(path)) return true;
        return false;
    }
    std::string trimmed(const std::string& s)
    {
        const std::size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        const std::size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }
    // The payment modes and order statuses from the requirements live
    // in ONE place for the whole project - domain/enums. The POS screen
    // just borrows the lists for its cycle buttons and checkout.
    const std::vector<std::string>& kPayModes = enums::paymentModeNames();
    const std::vector<std::string>& kStatuses = enums::orderStatusNames();
    // The project's barcodes/ folder: generated barcodes are saved
    // there and the file browser opens there. (tests/ is a separate
    // folder and is never used for generated output.)
    std::string barcodesDir()
    {
        const char* candidates[] = {"barcodes", "Barcode-Scanner/barcodes", "../barcodes"};
        for (const char* c : candidates)
            if (std::filesystem::is_directory(c)) return c;
        std::error_code ec;
        std::filesystem::create_directories("barcodes", ec);
        return "barcodes";
    }
    // Merchants that make sense per payment mode (POS drop-down).
    std::vector<std::string> merchantOptionsFor(const std::string& mode)
    {
        if (mode == "card") return {"ABL", "HBL", "MCB", "UBL", "Meezan", "Bank Alfalah"};
        if (mode == "wallet") return {"JazzCash", "EasyPaisa", "Raast"};
        return {"-"};  // cash has no merchant
    }
    std::string pkr(double v) { return "PKR " + stringutils::money(v); }
    std::string pkrShort(double v) { return "PKR " + stringutils::money(v); }
    std::string truncFront(const std::string& v, std::size_t maxChars)
    {
        if (v.size() <= maxChars) return v;
        return v.substr(0, maxChars - 1) + "~";
    }
    int parseQty(const std::string& text)
    {
        try { return text.empty() ? 1 : std::stoi(text); }
        catch (...) { return 0; }
    }
    std::string shorten(const std::string& s, std::size_t maxChars)
    {
        if (s.size() <= maxChars) return s;
        return "..." + s.substr(s.size() - (maxChars - 3));
    }
    std::string trimStr(const std::string& s)
    {
        const std::size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        const std::size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }
    bool parseDouble(const std::string& s, double& out)
    {
        try { out = std::stod(s); return true; }
        catch (...) { return false; }
    }
    bool parseIntStr(const std::string& s, int& out)
    {
        try { out = std::stoi(s); return true; }
        catch (...) { return false; }
    }
}

BarcodeStudioGui::BarcodeStudioGui()
    : m_window(sf::VideoMode(kWindowSize), "Chili Mili - Barcode POS Studio",
        sf::Style::Titlebar | sf::Style::Close | sf::Style::Resize),
      m_view(View::Generator),
      m_codeInput(m_font, {272.0f, 182.0f}, {308.0f, 46.0f}, "Enter 6 digits", true),
      m_fileNameInput(m_font, {272.0f, 430.0f}, {308.0f, 42.0f}, "barcode_123456.bmp"),
      m_generateRandom(m_font, "Random Code", {272.0f, 336.0f}, {150.0f, 40.0f}, false),
      m_generate(m_font, "Generate Barcode", {272.0f, 492.0f}, {308.0f, 48.0f}),
      m_pathInput(m_font, {272.0f, 146.0f}, {452.0f, 42.0f}, "Path to a .bmp file"),
      m_browse(m_font, "Browse .BMP...", {736.0f, 146.0f}, {212.0f, 42.0f}, false),
      m_decode(m_font, "Decode Image", {272.0f, 206.0f}, {200.0f, 44.0f}),
      m_decodeAttempted(false), m_decodeOk(false),
      m_cameraToggleBtn(m_font, "Start Camera", {246.0f, 84.0f}, {150.0f, 36.0f}, true),
      m_scanNextBtn(m_font, "Scan Another", {406.0f, 84.0f}, {190.0f, 36.0f}, false),
      m_camAddBtn(m_font, "Add to Order", {706.0f, 560.0f}, {200.0f, 38.0f}),
      m_fileAddBtn(m_font, "Add to Order", {644.0f, 522.0f}, {200.0f, 38.0f}),
      m_runTest(m_font, "Run Self-Test", {272.0f, 126.0f}, {220.0f, 44.0f}),
      m_testPassed(0),
      m_toast(m_font), m_hasPreview(false),
      m_cameraStatus("Standby"), m_newCameraFrameReady(false), m_newCodeScanned(false),
      m_hasCameraFrame(false),
      m_hasGeneratedBarcode(false),
      m_productRepo(), m_orderRepo(), m_detailRepo(), m_billRepo(),
      m_billDetailRepo(), m_categoryRepo(), m_seasonsRepo(),
      m_categoryService(m_categoryRepo),
      m_seasonsService(m_seasonsRepo),
      m_productService(m_productRepo, m_categoryService),
      m_orderService(m_productService, m_seasonsService, m_orderRepo, m_detailRepo,
          m_billRepo, m_billDetailRepo),
      m_reportService(m_orderRepo, m_billRepo, m_billDetailRepo, m_productRepo),
      m_posBarcode(m_font, {272.0f, 136.0f}, {250.0f, 42.0f}, "Barcode", true),
      m_posQty(m_font, {534.0f, 136.0f}, {56.0f, 42.0f}, "1", true),
      m_posCustomer(m_font, {750.0f, 332.0f}, {204.0f, 34.0f}, "Customer name"),
      m_posContact(m_font, {750.0f, 394.0f}, {204.0f, 34.0f}, "03001234567", true),
      m_posDescBtn(m_font, "Description", {272.0f, 192.0f}, {140.0f, 42.0f}, false),
      m_posAddBtn(m_font, "Add to Order", {424.0f, 192.0f}, {180.0f, 42.0f}),
      m_posCamBtn(m_font, "Start Cam", {602.0f, 136.0f}, {96.0f, 42.0f}, false),
      m_modeDrop(m_font, {750.0f, 444.0f}, {98.0f, 28.0f}),
      m_statusDrop(m_font, {856.0f, 444.0f}, {98.0f, 28.0f}),
      m_merchantDrop(m_font, {750.0f, 494.0f}, {204.0f, 28.0f}),
      m_posBillBtn(m_font, "Generate Bill", {272.0f, 566.0f}, {200.0f, 44.0f}),
      m_posNewBtn(m_font, "New Order", {484.0f, 566.0f}, {140.0f, 44.0f}, false),
      m_productsRefresh(m_font, "Refresh List", {770.0f, 22.0f}, {204.0f, 36.0f}, false),
      m_billCloseBtn(m_font, "Close Bill", {586.0f, 566.0f}, {180.0f, 40.0f}),
      m_productsPrev(m_font, "Prev", {756.0f, 584.0f}, {96.0f, 30.0f}, false),
      m_productsNext(m_font, "Next", {862.0f, 584.0f}, {100.0f, 30.0f}, false),
      m_roleBtn(m_font, "Cashier Mode", {246.0f, 22.0f}, {150.0f, 32.0f}, false),
      m_posSpecialPct(m_font, {750.0f, 546.0f}, {100.0f, 28.0f}, "Special % (<=8)", true),
      m_posAmount(m_font, {864.0f, 546.0f}, {140.0f, 28.0f}, "Amount received", true),
      m_posHoldBtn(m_font, "Hold Order", {272.0f, 618.0f}, {130.0f, 34.0f}, false),
      m_posResumeBtn(m_font, "Resume", {420.0f, 618.0f}, {100.0f, 34.0f}, false),
      m_heldDrop(m_font, {534.0f, 618.0f}, {180.0f, 34.0f}),
      m_catIdInput(m_font, {272.0f, 150.0f}, {60.0f, 40.0f}, "ID", true),
      m_catNameInput(m_font, {344.0f, 150.0f}, {240.0f, 40.0f}, "Name"),
      m_catAcronymInput(m_font, {596.0f, 150.0f}, {120.0f, 40.0f}, "Acr"),
      m_catAddBtn(m_font, "Add Category", {272.0f, 206.0f}, {180.0f, 40.0f}),
      m_catBackBtn(m_font, "Back", {900.0f, 22.0f}, {80.0f, 32.0f}, false),
      m_seasonStartInput(m_font, {272.0f, 150.0f}, {140.0f, 40.0f}, "YYYY-MM-DD"),
      m_seasonEndInput(m_font, {426.0f, 150.0f}, {140.0f, 40.0f}, "YYYY-MM-DD"),
      m_seasonPctInput(m_font, {580.0f, 150.0f}, {80.0f, 40.0f}, "0..100", true),
      m_seasonAddBtn(m_font, "Add Season", {272.0f, 206.0f}, {160.0f, 40.0f}),
      m_seasonToggleBtn(m_font, "Toggle Active", {444.0f, 206.0f}, {160.0f, 40.0f}, false),
      m_seasonBackBtn(m_font, "Back", {900.0f, 22.0f}, {80.0f, 32.0f}, false),
      m_invSearchInput(m_font, {272.0f, 150.0f}, {260.0f, 40.0f}, "Search acronym / name"),
      m_invQtyInput(m_font, {544.0f, 150.0f}, {80.0f, 40.0f}, "Qty", true),
      m_invSetBtn(m_font, "Set Stock", {636.0f, 150.0f}, {140.0f, 40.0f}),
      m_invBackBtn(m_font, "Back", {900.0f, 22.0f}, {80.0f, 32.0f}, false),
      m_peIdInput(m_font, {272.0f, 138.0f}, {80.0f, 36.0f}, "ID", true),
      m_peNameInput(m_font, {364.0f, 138.0f}, {260.0f, 36.0f}, "Name"),
      m_peAcronymInput(m_font, {636.0f, 138.0f}, {120.0f, 36.0f}, "Acronym"),
      m_pePriceInput(m_font, {272.0f, 196.0f}, {120.0f, 36.0f}, "Price", true),
      m_peTaxInput(m_font, {404.0f, 196.0f}, {120.0f, 36.0f}, "Tax", true),
      m_peDiscountInput(m_font, {536.0f, 196.0f}, {120.0f, 36.0f}, "Discount", true),
      m_peQtyInput(m_font, {668.0f, 196.0f}, {100.0f, 36.0f}, "Qty", true),
      m_peUnitDrop(m_font, {272.0f, 254.0f}, {140.0f, 32.0f}),
      m_peCategoryDrop(m_font, {424.0f, 254.0f}, {180.0f, 32.0f}),
      m_peSaveBtn(m_font, "Save", {272.0f, 316.0f}, {140.0f, 40.0f}),
      m_peBarcodeBtn(m_font, "Regenerate Barcode", {424.0f, 316.0f}, {220.0f, 40.0f}, false),
      m_peBackBtn(m_font, "Back", {900.0f, 22.0f}, {80.0f, 32.0f}, false),
      m_reportDrop(m_font, {272.0f, 128.0f}, {300.0f, 32.0f}),
      m_reportFromInput(m_font, {584.0f, 128.0f}, {140.0f, 32.0f}, "From YYYY-MM-DD"),
      m_reportToInput(m_font, {736.0f, 128.0f}, {140.0f, 32.0f}, "To YYYY-MM-DD"),
      m_reportRunBtn(m_font, "Run", {888.0f, 128.0f}, {80.0f, 32.0f}),
      m_reportBackBtn(m_font, "Back", {900.0f, 22.0f}, {80.0f, 32.0f}, false),
      m_reportPrevBtn(m_font, "Prev", {756.0f, 610.0f}, {96.0f, 28.0f}, false),
      m_reportNextBtn(m_font, "Next", {862.0f, 610.0f}, {100.0f, 28.0f}, false),
      m_heldResumeBtn(m_font, "Resume Selected", {272.0f, 566.0f}, {200.0f, 40.0f}),
      m_heldDeleteBtn(m_font, "Delete", {484.0f, 566.0f}, {140.0f, 40.0f}, false),
      m_heldBackBtn(m_font, "Back", {900.0f, 22.0f}, {80.0f, 32.0f}, false),
      m_heldPrevBtn(m_font, "Prev", {756.0f, 566.0f}, {96.0f, 28.0f}, false),
      m_heldNextBtn(m_font, "Next", {862.0f, 566.0f}, {100.0f, 28.0f}, false),
      m_oeOrderIdInput(m_font, {272.0f, 138.0f}, {180.0f, 36.0f}, "Order ID", true),
      m_oeCustomerInput(m_font, {272.0f, 196.0f}, {240.0f, 36.0f}, "Customer"),
      m_oeContactInput(m_font, {524.0f, 196.0f}, {200.0f, 36.0f}, "Contact"),
      m_oeLoadBtn(m_font, "Load", {464.0f, 138.0f}, {100.0f, 36.0f}),
      m_oeApproveBtn(m_font, "Manager Approve", {272.0f, 254.0f}, {200.0f, 40.0f}, false),
      m_oeSaveBtn(m_font, "Save Changes", {484.0f, 254.0f}, {180.0f, 40.0f}),
      m_oeBackBtn(m_font, "Back", {900.0f, 22.0f}, {80.0f, 32.0f}, false),
      m_printPdfBtn(m_font, "Print as PDF", {272.0f, 566.0f}, {180.0f, 40.0f}),
      m_printCloseBtn(m_font, "Close", {464.0f, 566.0f}, {140.0f, 40.0f}, false)
{
    if (!loadFont(m_font))
        throw std::runtime_error("Unable to load any font (looked in assets/fonts/ and system fonts).");
    m_codeInput.setMaxLength(6);
    m_fileNameInput.setMaxLength(80);
    m_posBarcode.setMaxLength(6);
    m_posQty.setMaxLength(3);
    m_posQty.setText("1");
    m_posCustomer.setMaxLength(40);
    m_posContact.setMaxLength(11);
    m_modeDrop.setOptions(kPayModes);
    m_modeDrop.setSelected(0);
    m_statusDrop.setOptions(kStatuses);
    m_statusDrop.setSelected(2);
    m_merchantDrop.setOptions(merchantOptionsFor("cash"));
    m_posSpecialPct.setMaxLength(5);
    m_posSpecialPct.setText("0");
    m_posAmount.setMaxLength(10);
    m_catIdInput.setMaxLength(2);
    m_catNameInput.setMaxLength(30);
    m_catAcronymInput.setMaxLength(8);
    m_seasonStartInput.setMaxLength(10);
    m_seasonEndInput.setMaxLength(10);
    m_seasonPctInput.setMaxLength(6);
    m_invSearchInput.setMaxLength(20);
    m_invQtyInput.setMaxLength(6);
    m_peIdInput.setMaxLength(6);
    m_peNameInput.setMaxLength(30);
    m_peAcronymInput.setMaxLength(8);
    m_pePriceInput.setMaxLength(10);
    m_peTaxInput.setMaxLength(8);
    m_peDiscountInput.setMaxLength(8);
    m_peQtyInput.setMaxLength(6);
    m_peUnitDrop.setOptions({"count", "weight"});
    m_peUnitDrop.setSelected(0);
    m_reportDrop.setOptions({"incomplete", "completed", "daily", "weekly",
        "monthly", "yearly", "custom", "sales_discounts", "top_sold", "top_stock"});
    m_reportDrop.setSelected(0);
    m_oeOrderIdInput.setMaxLength(8);
    m_oeCustomerInput.setMaxLength(40);
    m_oeContactInput.setMaxLength(11);
    m_codeInput.setFocused(true);
    m_window.setFramerateLimit(60);
    applyView();
    refreshProducts();
    refreshCategories();
    refreshSeasons();
    refreshInventory();
    refreshHeldOrders();
    m_window.setMinimumSize(sf::Vector2u(850u, 550u));
    initParticles();
}

BarcodeStudioGui::~BarcodeStudioGui() { stopCamera(); }

int BarcodeStudioGui::run()
{
    while (m_window.isOpen())
    {
        handleEvents();
        const float delta = m_clock.restart().asSeconds();
        update(delta);
        draw();
    }
    stopCamera();
    return 0;
}

bool BarcodeStudioGui::screenshot(const std::string& path, int view)
{
    if (view >= 0 && view < 14) switchView(static_cast<View>(view));
    if (m_view == View::SelfTest) runSelfTest();
    if (m_view == View::PosOrder)
    {
        // Seed a demo order so the POS screen can be QA'd visually.
        refreshProducts();
        m_posCustomer.setText("Ayesha Khan");
        m_descProduct = m_productService.findByBarcode("111001");
        m_orderService.addLine("111001", 2);
        m_orderService.addLine("129876", 1);
        m_posMessage = "Added Fresh Milk x2 to the order.";
        if (path.find("dropdown") != std::string::npos) m_statusDrop.setOpen(true);  // QA
        if (path.find("cam") != std::string::npos)  // QA: simulate a running camera
        {
            m_cameraRunning = true;
            Image frame;
            frame.width = 320; frame.height = 240;
            frame.pixels.assign(320u * 240u, 200);
            const Image bc = renderBarcode("123456", 2, 90);
            for (int y = 0; y < bc.height; ++y)
                for (int x = 0; x < bc.width; ++x)
                    frame.pixels[static_cast<std::size_t>(75 + y) * 320 + 55 + x] =
                        bc.pixels[static_cast<std::size_t>(y) * bc.width + x];
            updateCameraTexture(frame);
        }
        if (path.find("bill") != std::string::npos) posCheckout();  // QA: show the bill
        if (path.find("autoadd") != std::string::npos)  // QA: two camera scans of White Sugar
        {
            m_posBarcode.setText("145555");
            posDescribe();
            if (m_descProduct) posAddOne("145555");
            if (m_descProduct) posAddOne("145555");
        }
    }
    if (path.find("toorder") != std::string::npos)  // QA: a decoded product on Camera/File
    {
        if (m_view == View::CameraScanner) onBarcodeScanned("145555");
        if (m_view == View::FileScanner)
        {
            m_pathInput.setText("tests/barcode_145555.bmp");
            decodeFile();
        }
    }
    if (path.find("unknown") != std::string::npos)  // QA: scan a code with no product
    {
        if (m_view == View::CameraScanner) onBarcodeScanned("135790");
        if (m_view == View::FileScanner)
        {
            const Image img = renderBarcode("135790", 3, 120);
            saveBMP(img, barcodesDir() + "/barcode_135790.bmp");
            m_pathInput.setText("barcode_135790.bmp");
            decodeFile();
        }
    }
    m_transition = 1.0f;
    if (path.find("closed") != std::string::npos) { m_sidebarOpen = false; m_sidebarClosed = 1.0f; }  // QA
    if (path.find("wide") != std::string::npos) { m_window.setSize({1600u, 900u}); applyView(); }  // QA
    if (path.find("scroll") != std::string::npos && m_view == View::Products) m_productsScroll = 5;  // QA
    // Run ~1.5 s of frames so animations settle into their final state.
    for (int i = 0; i < 90; ++i) { update(0.016f); draw(); }
    sf::Texture texture;
    if (!texture.resize(m_window.getSize())) return false;
    texture.update(m_window);
    return texture.copyToImage().saveToFile(path);
}

// ------------------------------------------------------------- alive layer
void BarcodeStudioGui::initParticles()
{
    // No ambient particles: floating hearts/sparkles behind the text
    // were removed at the user's request (distracting). Only the
    // short celebration bursts on successful actions remain.
    m_ambient.clear();
    m_trail.clear();
}

void BarcodeStudioGui::updateParticles(float dt)
{
    for (Particle& p : m_burst)
    {
        p.life -= dt;
        p.vel.y += 130.0f * dt;              // gentle gravity
        p.vel *= (1.0f - 1.6f * dt);         // drag
        p.pos += p.vel * dt;
    }
    m_burst.erase(std::remove_if(m_burst.begin(), m_burst.end(),
        [](const Particle& p) { return p.life <= 0.0f; }), m_burst.end());
}

void BarcodeStudioGui::drawParticles()
{
    // Intentionally empty: the ambient hearts/sparkles and the mouse
    // trail were removed (they drifted behind headings and text).
}

void BarcodeStudioGui::drawBursts()
{
    for (const Particle& p : m_burst)
    {
        const float t = std::max(0.0f, p.life / p.maxLife);
        const std::uint8_t a = static_cast<std::uint8_t>(255.0f * t);
        const sf::Color c = p.kind == 1 ? sf::Color(199, 125, 255, a)
            : sf::Color(255, 92, 138, a);
        if (p.kind == 0) ui::theme::drawHeart(m_window, p.pos, p.size * 0.6f * t + 1.5f, c);
        else if (p.kind == 1) ui::theme::drawSparkle(m_window, p.pos, p.size * t + 1.0f, c);
        else
        {
            sf::CircleShape dot(p.size * 0.3f * t + 0.8f);
            dot.setPosition({p.pos.x - dot.getRadius(), p.pos.y - dot.getRadius()});
            dot.setFillColor(c);
            m_window.draw(dot);
        }
    }
}

void BarcodeStudioGui::spawnBurst(const sf::Vector2f& centre, int count)
{
    for (int i = 0; i < count; ++i)
    {
        Particle p;
        const float ang = frand(0.0f, 6.283f);
        const float speed = frand(45.0f, 185.0f);
        p.pos = centre;
        p.vel = {std::cos(ang) * speed, std::sin(ang) * speed - 55.0f};
        p.size = frand(4.0f, 9.0f);
        p.phase = 0.0f;
        p.maxLife = p.life = frand(0.55f, 1.15f);
        p.kind = static_cast<int>(frand(0.0f, 2.99f));
        m_burst.push_back(p);
    }
}

void BarcodeStudioGui::celebrate(const std::string& bubble, const sf::Vector2f& burstAt)
{
    m_mascotText = bubble;
    m_mascotHappy = 3.0f;
    spawnBurst(burstAt);
    spawnBurst({110.0f, 486.0f}, 14);  // mascot joins in
}

void BarcodeStudioGui::switchView(View next)
{
    if (next == m_view) return;
    const bool fromCamView = (m_view == View::CameraScanner || m_view == View::PosOrder);
    const bool toCamView = (next == View::CameraScanner || next == View::PosOrder);
    if (fromCamView && !toCamView) stopCamera();
    else if (fromCamView && toCamView) resetScanState();
    if (next == View::PosOrder || next == View::Products) refreshProducts();
    if (next == View::Categories) refreshCategories();
    if (next == View::Seasons) refreshSeasons();
    if (next == View::Inventory) refreshInventory();
    if (next == View::HeldOrders) refreshHeldOrders();
    if (next == View::ProductEdit) refreshCategories();
    m_modeDrop.close();
    m_statusDrop.close();
    m_merchantDrop.close();
    m_peUnitDrop.close();
    m_peCategoryDrop.close();
    m_reportDrop.close();
    m_heldDrop.close();
    m_view = next;
    m_transition = 0.0f;
    switch (next)
    {
    case View::Generator:     m_mascotText = "Let's create a\nbarcode!"; break;
    case View::FileScanner:   m_mascotText = "Show me a BMP\nto decode!"; break;
    case View::CameraScanner: m_mascotText = "Point the camera...\nI'll watch!"; break;
    case View::PosOrder:      m_mascotText = "Let's ring up\nan order!"; break;
    case View::Products:      m_mascotText = "Our shelves,\nall stocked!"; break;
    case View::SelfTest:      m_mascotText = "Fingers crossed...\n6 out of 6!"; break;
    case View::Categories:    m_mascotText = "Set up the\ncategories!"; break;
    case View::Seasons:       m_mascotText = "Seasonal\ndiscounts!"; break;
    case View::Inventory:     m_mascotText = "Stock check\ntime!"; break;
    case View::ProductEdit:   m_mascotText = "Add or edit\na product!"; break;
    case View::Reports:       m_mascotText = "Numbers,\nnumbers!"; break;
    case View::HeldOrders:    m_mascotText = "Resume a\nheld order!"; break;
    case View::OrderEdit:     m_mascotText = "Editing a\npaid order!"; break;
    case View::BillPrint:     m_mascotText = "Here's the\nbill!"; break;
    }
    if (next == View::Generator) m_codeInput.setFocused(true);
    else if (next == View::FileScanner) m_pathInput.setFocused(true);
    else if (next == View::PosOrder) m_posBarcode.setFocused(true);
    else if (next == View::ProductEdit) m_peIdInput.setFocused(true);
    else if (next == View::OrderEdit) m_oeOrderIdInput.setFocused(true);
}

void BarcodeStudioGui::drawMascot()
{
    // Speech bubble
    ui::theme::drawRoundedBox(sidebarRt(), {14.0f, 384.0f}, {192.0f, 52.0f}, 12.0f,
        ui::theme::White, ui::theme::Border, 1.0f);
    sf::ConvexShape tail;
    tail.setPointCount(3);
    tail.setPoint(0, {92.0f, 436.0f});
    tail.setPoint(1, {112.0f, 436.0f});
    tail.setPoint(2, {100.0f, 448.0f});
    tail.setFillColor(ui::theme::White);
    sidebarRt().draw(tail);
    sf::Text bubble(m_font, m_mascotText, ui::theme::scaledSize(12));
    ui::theme::applyTextScale(bubble);
    bubble.setFillColor(ui::theme::Text);
    bubble.setPosition({26.0f, 392.0f});
    sidebarRt().draw(bubble);

    const float bob = m_mascotHappy > 0.0f
        ? std::abs(std::sin(m_elapsed * 9.0f)) * -10.0f
        : std::sin(m_elapsed * 2.0f) * 3.0f;
    const sf::Vector2f c{110.0f, 508.0f + bob};

    // Ears
    for (float sx : {-1.0f, 1.0f})
    {
        sf::ConvexShape ear;
        ear.setPointCount(3);
        ear.setPoint(0, {c.x + sx * 34.0f, c.y - 26.0f});
        ear.setPoint(1, {c.x + sx * 16.0f, c.y - 30.0f});
        ear.setPoint(2, {c.x + sx * 30.0f, c.y - 48.0f});
        ear.setFillColor(sf::Color(0xFFB3CDFF));
        sidebarRt().draw(ear);
    }
    // Body (soft pink blob)
    sf::CircleShape body(42.0f);
    body.setPosition({c.x - 42.0f, c.y - 42.0f});
    body.setFillColor(sf::Color(0xFFC7DAFF));
    body.setOutlineColor(ui::theme::Primary);
    body.setOutlineThickness(2.0f);
    sidebarRt().draw(body);
    // Bow
    ui::theme::drawHeart(sidebarRt(), {c.x + 30.0f, c.y - 40.0f}, 7.0f, ui::theme::Primary);
    // Eyes — pupils follow the mouse; blink every ~3.7 s
    const float blink = std::fmod(m_elapsed, 3.7f) < 0.12f ? 0.12f : 1.0f;
    const sf::Vector2f mouseLocal{m_mouse.x + ui::theme::SidebarWidth * m_sidebarClosed, m_mouse.y};
    sf::Vector2f look{mouseLocal.x - c.x, mouseLocal.y - c.y};
    const float len = std::sqrt(look.x * look.x + look.y * look.y);
    if (len > 1.0f) look = {look.x / len * 3.2f, look.y / len * 3.2f};
    for (float sx : {-1.0f, 1.0f})
    {
        const sf::Vector2f eye{c.x + sx * 15.0f, c.y - 8.0f};
        sf::CircleShape white(7.5f);
        white.setScale({1.0f, blink});
        white.setPosition({eye.x - 7.5f, eye.y - 7.5f * blink});
        white.setFillColor(sf::Color::White);
        sidebarRt().draw(white);
        if (blink > 0.5f)
        {
            sf::CircleShape pupil(3.6f);
            pupil.setPosition({eye.x - 3.6f + look.x, eye.y - 3.6f + look.y});
            pupil.setFillColor(ui::theme::Heading);
            sidebarRt().draw(pupil);
        }
    }
    // Cheeks + heart mouth
    for (float sx : {-1.0f, 1.0f})
    {
        sf::CircleShape cheek(6.0f);
        cheek.setPosition({c.x + sx * 27.0f - 6.0f, c.y + 4.0f});
        cheek.setFillColor(sf::Color(255, 92, 138, 110));
        sidebarRt().draw(cheek);
    }
    ui::theme::drawHeart(sidebarRt(), {c.x, c.y + 10.0f}, 5.0f, ui::theme::PrimaryPressed);
    // The barcode card she is holding
    sf::RectangleShape card({66.0f, 38.0f});
    card.setOrigin({33.0f, 19.0f});
    card.setPosition({c.x, c.y + 46.0f});
    card.setRotation(sf::degrees(-4.0f));
    card.setFillColor(sf::Color::White);
    card.setOutlineColor(ui::theme::Border);
    card.setOutlineThickness(1.0f);
    sidebarRt().draw(card);
    const float barW[10] = {3, 1, 2, 1, 4, 1, 2, 3, 1, 2};
    float bx = c.x - 26.0f;
    for (int i = 0; i < 10; ++i)
    {
        sf::RectangleShape bar({barW[i], 24.0f});
        bar.setPosition({bx, c.y + 46.0f - 12.0f});
        bar.setFillColor(sf::Color::Black);
        sidebarRt().draw(bar);
        bx += barW[i] + 2.6f;
    }
}

// ------------------------------------------------- responsive / fullscreen
// The whole UI is designed on a 1000x680 canvas. applyView() scales that
// canvas to FIT any window size (maximised or fullscreen) and centres it,
// so the layout fills the screen proportionally instead of huddling in
// one corner. Mouse mapping goes through the same view, so clicks stay
// exact at every size.
void BarcodeStudioGui::applyView()
{
    const sf::Vector2u win = m_window.getSize();
    if (win.x == 0 || win.y == 0) return;
    // Fill the WHOLE window: the layout height stays 680 units, the
    // layout width grows with the window, so there are no empty pink
    // letterbox bars and nothing floats on a background.
    const float winAspect = static_cast<float>(win.x) / static_cast<float>(win.y);
    if (winAspect >= 1000.0f / 680.0f)
    {
        m_renderScale = static_cast<float>(win.y) / 680.0f;
        m_w = static_cast<float>(win.x) / m_renderScale;
        m_h = 680.0f;
    }
    else
    {
        m_renderScale = static_cast<float>(win.x) / 1000.0f;
        m_w = 1000.0f;
        m_h = static_cast<float>(win.y) / m_renderScale;
    }
    ui::theme::uiScale = m_renderScale;  // crisp text (see Theme.h)
    m_window.setView(sf::View(sf::FloatRect({0.0f, 0.0f}, {m_w, m_h})));
    // The compositing textures live at the window's native pixel size,
    // so shapes AND text render at full resolution - no magnification
    // blur when the window is maximised.
    if (m_contentTexture.getSize() != win)
        (void)m_contentTexture.resize(win);
    m_contentTexture.setView(sf::View(sf::FloatRect({0.0f, 0.0f}, {m_w, m_h})));
    const sf::Vector2u sbSize{static_cast<unsigned int>(220.0f * m_renderScale + 0.5f), win.y};
    if (m_sidebarTexture.getSize() != sbSize)
        (void)m_sidebarTexture.resize(sbSize);
    m_sidebarTexture.setView(sf::View(sf::FloatRect({0.0f, 0.0f}, {220.0f, m_h})));
}

void BarcodeStudioGui::toggleFullscreen()
{
    m_fullscreen = !m_fullscreen;
    if (m_fullscreen)
        m_window.create(sf::VideoMode::getDesktopMode(),
            "Chili Mili - Barcode POS Studio", sf::Style::Default, sf::State::Fullscreen);
    else
        m_window.create(sf::VideoMode(kWindowSize),
            "Chili Mili - Barcode POS Studio",
            sf::Style::Titlebar | sf::Style::Close | sf::Style::Resize);
    m_window.setFramerateLimit(60);
    applyView();
}

bool BarcodeStudioGui::anyTextFocused() const
{
    return m_codeInput.isFocused() || m_fileNameInput.isFocused()
        || m_pathInput.isFocused() || m_posBarcode.isFocused()
        || m_posQty.isFocused() || m_posCustomer.isFocused()
        || m_posContact.isFocused() || m_posSpecialPct.isFocused()
        || m_posAmount.isFocused() || m_catIdInput.isFocused()
        || m_catNameInput.isFocused() || m_catAcronymInput.isFocused()
        || m_seasonStartInput.isFocused() || m_seasonEndInput.isFocused()
        || m_seasonPctInput.isFocused() || m_invSearchInput.isFocused()
        || m_invQtyInput.isFocused() || m_peIdInput.isFocused()
        || m_peNameInput.isFocused() || m_peAcronymInput.isFocused()
        || m_pePriceInput.isFocused() || m_peTaxInput.isFocused()
        || m_peDiscountInput.isFocused() || m_peQtyInput.isFocused()
        || m_reportFromInput.isFocused() || m_reportToInput.isFocused()
        || m_oeOrderIdInput.isFocused() || m_oeCustomerInput.isFocused()
        || m_oeContactInput.isFocused();
}

// ------------------------------------------------------------------- events
void BarcodeStudioGui::handleEvents()
{
    while (const std::optional<sf::Event> event = m_window.pollEvent())
    {
        if (event->is<sf::Event::Closed>())
        {
            stopCamera();
            m_window.close();
            continue;
        }
        if (event->getIf<sf::Event::Resized>())
            applyView();
        if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>())
        {
            if (m_view == View::Products)
            {
                const int maxScroll = std::max(0, static_cast<int>(m_productsCache.size()) - 8);
                m_productsScroll = std::clamp(
                    m_productsScroll - static_cast<int>(wheel->delta), 0, maxScroll);
            }
        }
        if (const auto* moved = event->getIf<sf::Event::MouseMoved>())
        {
            m_mouse = m_window.mapPixelToCoords(moved->position);
        }
        if (const auto* text = event->getIf<sf::Event::TextEntered>())
        {
            m_codeInput.handleTextEntered(text->unicode);
            m_fileNameInput.handleTextEntered(text->unicode);
            m_pathInput.handleTextEntered(text->unicode);
            m_posBarcode.handleTextEntered(text->unicode);
            m_posQty.handleTextEntered(text->unicode);
            m_posCustomer.handleTextEntered(text->unicode);
            m_posContact.handleTextEntered(text->unicode);
            m_posSpecialPct.handleTextEntered(text->unicode);
            m_posAmount.handleTextEntered(text->unicode);
            m_catIdInput.handleTextEntered(text->unicode);
            m_catNameInput.handleTextEntered(text->unicode);
            m_catAcronymInput.handleTextEntered(text->unicode);
            m_seasonStartInput.handleTextEntered(text->unicode);
            m_seasonEndInput.handleTextEntered(text->unicode);
            m_seasonPctInput.handleTextEntered(text->unicode);
            m_invSearchInput.handleTextEntered(text->unicode);
            m_invQtyInput.handleTextEntered(text->unicode);
            m_peIdInput.handleTextEntered(text->unicode);
            m_peNameInput.handleTextEntered(text->unicode);
            m_peAcronymInput.handleTextEntered(text->unicode);
            m_pePriceInput.handleTextEntered(text->unicode);
            m_peTaxInput.handleTextEntered(text->unicode);
            m_peDiscountInput.handleTextEntered(text->unicode);
            m_peQtyInput.handleTextEntered(text->unicode);
            m_reportFromInput.handleTextEntered(text->unicode);
            m_reportToInput.handleTextEntered(text->unicode);
            m_oeOrderIdInput.handleTextEntered(text->unicode);
            m_oeCustomerInput.handleTextEntered(text->unicode);
            m_oeContactInput.handleTextEntered(text->unicode);
        }
        if (const auto* key = event->getIf<sf::Event::KeyPressed>())
        {
            if (key->code == sf::Keyboard::Key::Enter) handleEnterKey();
            else if (key->code == sf::Keyboard::Key::Escape)
            {
                m_codeInput.setFocused(false);
                m_fileNameInput.setFocused(false);
                m_pathInput.setFocused(false);
                m_posBarcode.setFocused(false);
                m_posQty.setFocused(false);
                m_posCustomer.setFocused(false);
                m_posContact.setFocused(false);
            }
            else if (key->code == sf::Keyboard::Key::F11) toggleFullscreen();
            // Number keys 1-6 are screen shortcuts ONLY when no text box
            // is being edited - otherwise the digit belongs to the box
            // (TextEntered types it) and must not switch screens.
            else if (anyTextFocused()) { /* digit goes to the text box */ }
            else if (key->code == sf::Keyboard::Key::Num1) switchView(View::Generator);
            else if (key->code == sf::Keyboard::Key::Num2) switchView(View::FileScanner);
            else if (key->code == sf::Keyboard::Key::Num3) switchView(View::CameraScanner);
            else if (key->code == sf::Keyboard::Key::Num4) switchView(View::PosOrder);
            else if (key->code == sf::Keyboard::Key::Num5) switchView(View::Products);
            else if (key->code == sf::Keyboard::Key::Num6) switchView(View::SelfTest);
        }
        if (const auto* mouse = event->getIf<sf::Event::MouseButtonPressed>())
        {
            if (mouse->button != sf::Mouse::Button::Left) continue;
            sf::Vector2f pos = m_window.mapPixelToCoords(mouse->position);
            m_mouse = pos;
            // Ignore clicks in the letterbox bars of a maximised window.
            if (pos.x < 0.0f || pos.x > m_w || pos.y < 0.0f || pos.y > m_h) continue;

            // The sidebar open/close button lives on the window itself.
            if (sidebarToggleRect().contains(pos))
            {
                m_sidebarOpen = !m_sidebarOpen;
                continue;
            }

                        // Sidebar clicks use sidebar-local coordinates (it slides).
            const float visibleSidebar = ui::theme::SidebarWidth * (1.0f - m_sidebarClosed);
            if (pos.x < visibleSidebar)
            {
                const sf::Vector2f spos{pos.x + ui::theme::SidebarWidth * m_sidebarClosed, pos.y};
                if (spos.y >= 606.0f && spos.y < 654.0f)
                {
                    stopCamera();
                    m_window.close();
                    continue;
                }
                // Role pill
                if (spos.y >= 60.0f && spos.y < 88.0f)
                {
                    m_role = (m_role == UserRole::Manager)
                        ? UserRole::Cashier : UserRole::Manager;
                    if (m_role == UserRole::Cashier)
                    {
                        const View managerViews[] = {View::Categories, View::Seasons,
                            View::Inventory, View::ProductEdit, View::Reports};
                        for (View v : managerViews)
                            if (m_view == v) { switchView(View::PosOrder); break; }
                    }
                    continue;
                }
                // Nav entries (must mirror the list in drawSidebar)
                struct NavEntry { View v; bool managerOnly; };
                static const NavEntry kNav[] = {
                    {View::Generator,     false},
                    {View::FileScanner,   false},
                    {View::CameraScanner, false},
                    {View::PosOrder,      false},
                    {View::HeldOrders,    false},
                    {View::Products,      false},
                    {View::OrderEdit,     false},
                    {View::Categories,    true},
                    {View::Seasons,       true},
                    {View::Inventory,     true},
                    {View::ProductEdit,   true},
                    {View::Reports,       true},
                    {View::SelfTest,      false},
                };
                const bool isManager = (m_role == UserRole::Manager);
                float y = 100.0f;
                for (const NavEntry& e : kNav)
                {
                    if (e.managerOnly && !isManager) continue;
                    if (spos.y >= y && spos.y < y + 30.0f) { switchView(e.v); break; }
                    y += 34.0f;
                }
                continue;
            }

            // Role pill on every screen (top-left, next to the heading)
            if (sf::FloatRect({kLeft, 22.0f}, {150.0f, 32.0f}).contains(pos))
            {
                m_role = (m_role == UserRole::Manager)
                    ? UserRole::Cashier : UserRole::Manager;
                if (m_role == UserRole::Cashier)
                {
                    const View managerViews[] = {View::Categories, View::Seasons,
                        View::Inventory, View::ProductEdit, View::Reports};
                    for (View v : managerViews)
                        if (m_view == v) { switchView(View::PosOrder); break; }
                }
                continue;
            }

            if (m_view == View::Generator)
            {
                m_codeInput.handleClick(pos);
                m_fileNameInput.handleClick(pos);
                // Clicking the digit tiles focuses the code box.
                if (sf::FloatRect({272.0f, 262.0f}, {308.0f, 58.0f}).contains(pos))
                    m_codeInput.setFocused(true);
                if (m_generateRandom.handleClick(pos))
                {
                    std::uniform_int_distribution<int> digit(0, 9);
                    std::string code(6, '0');
                    code[0] = static_cast<char>('1' + rng()() % 9);
                    for (int i = 1; i < 6; ++i) code[i] = static_cast<char>('0' + digit(rng()));
                    m_codeInput.setText(code);
                    m_mascotText = "Ooh, a lucky\nrandom code!";
                    m_mascotHappy = 1.2f;
                }
                else if (m_generate.handleClick(pos)) generateBarcode();
            }
            else if (m_view == View::FileScanner)
            {
                m_pathInput.handleClick(pos);
                if (m_browse.handleClick(pos)) browseFile();
                else if (m_decode.handleClick(pos)) decodeFile();
                else if (m_decodeOk && m_descProduct && m_fileAddBtn.handleClick(pos))
                    addDecodedToOrder();
            }
            else if (m_view == View::CameraScanner)
            {
                if (m_cameraToggleBtn.handleClick(pos))
                {
                    if (m_cameraRunning) stopCamera(); else startCamera();
                }
                else if (m_scanNextBtn.handleClick(pos)) resetScan();
                else if (m_hasGeneratedBarcode && m_descProduct && m_camAddBtn.handleClick(pos))
                    addDecodedToOrder();
            }
            else if (m_view == View::PosOrder)
            {
                if (m_showBill)
                {
                    if (m_billCloseBtn.handleClick(pos))
                    {
                        m_showBill = false;
                        resetScanState();
                        if (m_cameraRunning) { stopCamera(); startCamera(); }
                    }
                    else if (m_printPdfBtn.handleClick(pos))
                    {
                        posPrintPdf();
                    }
                }
                else
                {
                    m_posBarcode.handleClick(pos);
                    m_posQty.handleClick(pos);
                    m_posCustomer.handleClick(pos);
                    m_posContact.handleClick(pos);
                    // Drop-downs next: an open list swallows the click.
                    if (m_modeDrop.handleClick(pos) || m_statusDrop.handleClick(pos)
                        || m_merchantDrop.handleClick(pos))
                    {
                        if (m_modeDrop.selected() != m_modeIdxCache)
                        {
                            m_modeIdxCache = m_modeDrop.selected();
                            m_merchantDrop.setOptions(merchantOptionsFor(
                                kPayModes[static_cast<std::size_t>(m_modeIdxCache)]));
                            m_merchantDrop.setSelected(0);
                        }
                    }
                    else if (m_posCamBtn.handleClick(pos))
                    {
                        if (m_cameraRunning) stopCamera(); else startCamera();
                    }
                    else if (m_posDescBtn.handleClick(pos)) posDescribe();
                    else if (m_posAddBtn.handleClick(pos)) posAdd();
                    else if (m_posBillBtn.handleClick(pos)) posCheckout();
                    else if (m_posNewBtn.handleClick(pos)) posNewOrder();
                    else if (m_posHoldBtn.handleClick(pos)) posHold();
                    else if (m_posResumeBtn.handleClick(pos)) posResume();
                    else if (m_heldDrop.handleClick(pos)) {}
                    else
                    {
                        // Cart row controls: [-] qty [+] ... [x]
                        const auto& lines = m_orderService.lines();
                        for (std::size_t i = 0; i < lines.size() && i < 4; ++i)
                        {
                            const float ry = 352.0f + static_cast<float>(i) * 48.0f;
                            if (pos.y < ry || pos.y >= ry + 48.0f) continue;
                            const float crx = m_w - 1000.0f;
                            if (sf::FloatRect({488.0f + crx, ry + 10.0f}, {26.0f, 26.0f}).contains(pos))
                                m_orderService.changeQuantity(i, -1);
                            else if (sf::FloatRect({540.0f + crx, ry + 10.0f}, {26.0f, 26.0f}).contains(pos))
                                m_orderService.changeQuantity(i, +1);
                            else if (sf::FloatRect({642.0f + crx, ry + 10.0f}, {24.0f, 26.0f}).contains(pos))
                                m_orderService.removeLine(i);
                        }
                    }
                }
            }
            else if (m_view == View::Products)
            {
                const int maxScroll = std::max(0, static_cast<int>(m_productsCache.size()) - 8);
                if (m_productsRefresh.handleClick(pos)) refreshProducts();
                else if (m_productsPrev.handleClick(pos))
                    m_productsScroll = std::max(0, m_productsScroll - 8);
                else if (m_productsNext.handleClick(pos))
                    m_productsScroll = std::min(maxScroll, m_productsScroll + 8);
            }
                        else if (m_view == View::SelfTest)
            {
                if (m_runTest.handleClick(pos)) runSelfTest();
            }
            else if (m_view == View::Categories)
            {
                m_catIdInput.handleClick(pos);
                m_catNameInput.handleClick(pos);
                m_catAcronymInput.handleClick(pos);
                if (m_catAddBtn.handleClick(pos)) categoryAdd();
                else if (m_catBackBtn.handleClick(pos)) switchView(View::PosOrder);
            }
            else if (m_view == View::Seasons)
            {
                m_seasonStartInput.handleClick(pos);
                m_seasonEndInput.handleClick(pos);
                m_seasonPctInput.handleClick(pos);
                if (m_seasonAddBtn.handleClick(pos)) seasonAdd();
                else if (m_seasonToggleBtn.handleClick(pos)) seasonToggle();
                else if (m_seasonBackBtn.handleClick(pos)) switchView(View::PosOrder);
                else
                {
                    for (std::size_t i = 0; i < m_seasonsCache.size() && i < 6; ++i)
                    {
                        const float ry = 320.0f + static_cast<float>(i) * 44.0f;
                        if (pos.y >= ry && pos.y < ry + 40.0f)
                        {
                            m_seasonSelectedIdx = static_cast<int>(i);
                            break;
                        }
                    }
                }
            }
            else if (m_view == View::Inventory)
            {
                m_invSearchInput.handleClick(pos);
                m_invQtyInput.handleClick(pos);
                if (m_invSetBtn.handleClick(pos)) inventorySet();
                else if (m_invBackBtn.handleClick(pos)) switchView(View::PosOrder);
                else
                {
                    const std::string filter = trimStr(m_invSearchInput.getText());
                    int shown = 0;
                    for (std::size_t i = 0; i < m_productsCache.size(); ++i)
                    {
                        const Product& p = m_productsCache[i];
                        if (!filter.empty()
                            && p.acronym.find(filter) == std::string::npos
                            && p.name.find(filter) == std::string::npos) continue;
                        const float ry = 200.0f + static_cast<float>(shown) * 44.0f;
                        if (pos.y >= ry && pos.y < ry + 40.0f)
                        {
                            m_invSelectedIdx = static_cast<int>(i);
                            m_invQtyInput.setText(std::to_string(p.availableQuantity));
                            break;
                        }
                        ++shown;
                        if (shown >= 8) break;
                    }
                }
            }
            else if (m_view == View::ProductEdit)
            {
                m_peIdInput.handleClick(pos);
                m_peNameInput.handleClick(pos);
                m_peAcronymInput.handleClick(pos);
                m_pePriceInput.handleClick(pos);
                m_peTaxInput.handleClick(pos);
                m_peDiscountInput.handleClick(pos);
                m_peQtyInput.handleClick(pos);
                bool rowHit = false;
                {
                    float ry = 464.0f;
                    int shown = 0;
                    for (const Product& p : m_productsCache)
                    {
                        if (shown >= 4) break;
                        if (pos.y >= ry && pos.y < ry + 36.0f)
                        {
                            m_peIdInput.setText(std::to_string(p.id));
                            productEditLoad();
                            rowHit = true;
                            break;
                        }
                        ry += 40.0f;
                        ++shown;
                    }
                }
                if (rowHit) { }
                else if (m_peUnitDrop.handleClick(pos)) {}
                else if (m_peCategoryDrop.handleClick(pos)) {}
                else if (m_peSaveBtn.handleClick(pos)) productEditSave();
                else if (m_peBarcodeBtn.handleClick(pos)) productEditGenerateBarcode();
                else if (m_peBackBtn.handleClick(pos)) switchView(View::PosOrder);
            }
            else if (m_view == View::Reports)
            {
                m_reportFromInput.handleClick(pos);
                m_reportToInput.handleClick(pos);
                if (m_reportDrop.handleClick(pos)) {}
                else if (m_reportRunBtn.handleClick(pos)) runReport();
                else if (m_reportBackBtn.handleClick(pos)) switchView(View::PosOrder);
                else if (m_reportPrevBtn.handleClick(pos))
                    m_reportScroll = std::max(0, m_reportScroll - 8);
                else if (m_reportNextBtn.handleClick(pos))
                {
                    const int maxScroll = std::max(0, static_cast<int>(m_reportTable.rows.size()) - 8);
                    m_reportScroll = std::min(maxScroll, m_reportScroll + 8);
                }
            }
            else if (m_view == View::HeldOrders)
            {
                if (m_heldResumeBtn.handleClick(pos)) heldResumeSelected();
                else if (m_heldDeleteBtn.handleClick(pos)) heldDeleteSelected();
                else if (m_heldBackBtn.handleClick(pos)) switchView(View::PosOrder);
                else if (m_heldPrevBtn.handleClick(pos))
                    m_heldScroll = std::max(0, m_heldScroll - 8);
                else if (m_heldNextBtn.handleClick(pos))
                {
                    const int maxScroll = std::max(0, static_cast<int>(m_heldCache.size()) - 8);
                    m_heldScroll = std::min(maxScroll, m_heldScroll + 8);
                }
                else
                {
                    for (std::size_t i = 0; i < m_heldCache.size() && i < 8; ++i)
                    {
                        const std::size_t row = static_cast<std::size_t>(m_heldScroll) + i;
                        if (row >= m_heldCache.size()) break;
                        const float ry = 200.0f + static_cast<float>(i) * 44.0f;
                        if (pos.y >= ry && pos.y < ry + 40.0f)
                        {
                            m_heldDrop.setSelected(static_cast<int>(row));
                            break;
                        }
                    }
                }
            }
            else if (m_view == View::OrderEdit)
            {
                m_oeOrderIdInput.handleClick(pos);
                m_oeCustomerInput.handleClick(pos);
                m_oeContactInput.handleClick(pos);
                if (m_oeLoadBtn.handleClick(pos)) orderEditLoad();
                else if (m_oeApproveBtn.handleClick(pos)) orderEditApprove();
                else if (m_oeSaveBtn.handleClick(pos)) orderEditSave();
                else if (m_oeBackBtn.handleClick(pos)) switchView(View::PosOrder);
                else
                {
                    // Quantity steppers on the loaded order's rows
                    // (rows are drawn from y=404 in steps of 44, max 4).
                    float ry = 404.0f;
                    for (std::size_t i = 0; i < m_oeLines.size() && i < 4; ++i)
                    {
                        if (sf::FloatRect({636.0f, ry}, {30.0f, 40.0f}).contains(pos))
                        {
                            if (m_oeLines[i].second > 1) m_oeLines[i].second -= 1;
                            else m_oeLines.erase(m_oeLines.begin() + static_cast<long>(i));
                            break;
                        }
                        if (sf::FloatRect({706.0f, ry}, {30.0f, 40.0f}).contains(pos))
                        {
                            // Cannot exceed shelf stock + what this
                            // order already holds of the product.
                            const int cap = m_oeLines[i].first.availableQuantity + m_oeLines[i].second;
                            if (m_oeLines[i].second < cap) m_oeLines[i].second += 1;
                            break;
                        }
                        ry += 44.0f;
                    }
                }
            }
            else if (m_view == View::BillPrint)
            {
                if (m_printPdfBtn.handleClick(pos)) posPrintPdf();
                else if (m_printCloseBtn.handleClick(pos)) switchView(View::PosOrder);
            }
        }
    }
}

void BarcodeStudioGui::handleEnterKey()
{
    switch (m_view)
    {
    case View::Generator:
        if (m_generate.isEnabled()) generateBarcode();
        else
        {
            m_shake = 0.4f;
            showToast("Enter exactly 6 digits (first digit 1-9).", false);
        }
        break;
    case View::FileScanner:   decodeFile(); break;
    case View::CameraScanner: if (m_scanLocked) resetScan(); break;
    case View::PosOrder:      posAdd(); break;
    case View::Products:      break;
    case View::SelfTest:      runSelfTest(); break;
    }
}

// ------------------------------------------------------------------- update
void BarcodeStudioGui::update(float deltaSeconds)
{
    m_elapsed += deltaSeconds;
    if (m_transition < 1.0f) m_transition = std::min(1.0f, m_transition + deltaSeconds / 0.32f);
    if (m_shake > 0.0f) m_shake = std::max(0.0f, m_shake - deltaSeconds);
    if (m_mascotHappy > 0.0f) m_mascotHappy -= deltaSeconds;
    m_testRunTime += deltaSeconds;
    for (float& p : m_tilePop) p = std::max(0.0f, p - deltaSeconds * 2.6f);
    updateParticles(deltaSeconds);

    m_mouse = m_window.mapPixelToCoords(sf::Mouse::getPosition(m_window));
    // Animate the sidebar slide (0 = open, 1 = closed).
    const float sbTarget = m_sidebarOpen ? 0.0f : 1.0f;
    const float sbStep = deltaSeconds / 0.25f;
    if (m_sidebarClosed < sbTarget) m_sidebarClosed = std::min(sbTarget, m_sidebarClosed + sbStep);
    else if (m_sidebarClosed > sbTarget) m_sidebarClosed = std::max(sbTarget, m_sidebarClosed - sbStep);
    const sf::Vector2f cpos = contentMouse();  // widgets live in content space
    m_codeInput.update(deltaSeconds);
    m_fileNameInput.update(deltaSeconds);
    m_pathInput.update(deltaSeconds);
    m_generateRandom.update(cpos, deltaSeconds);
    m_generate.update(cpos, deltaSeconds);
    m_browse.update(cpos, deltaSeconds);
    m_decode.update(cpos, deltaSeconds);
    m_cameraToggleBtn.update(cpos, deltaSeconds);
    m_scanNextBtn.update(cpos, deltaSeconds);
    m_camAddBtn.update(cpos, deltaSeconds);
    m_fileAddBtn.update(cpos, deltaSeconds);
    m_runTest.update(cpos, deltaSeconds);
    m_posBarcode.update(deltaSeconds);
    m_posQty.update(deltaSeconds);
    m_posCustomer.update(deltaSeconds);
    m_posContact.update(deltaSeconds);
    m_modeDrop.update(cpos, deltaSeconds);
    m_statusDrop.update(cpos, deltaSeconds);
    m_merchantDrop.update(cpos, deltaSeconds);
    m_posDescBtn.update(cpos, deltaSeconds);
    m_posAddBtn.update(cpos, deltaSeconds);
    m_posCamBtn.update(cpos, deltaSeconds);
    m_posBillBtn.update(cpos, deltaSeconds);
    m_posNewBtn.update(cpos, deltaSeconds);
    m_productsRefresh.update(cpos, deltaSeconds);
    m_productsPrev.update(cpos, deltaSeconds);
    m_productsNext.update(cpos, deltaSeconds);
        m_roleBtn.update(cpos, deltaSeconds);
    m_posSpecialPct.update(deltaSeconds);
    m_posAmount.update(deltaSeconds);
    m_posHoldBtn.update(cpos, deltaSeconds);
    m_posResumeBtn.update(cpos, deltaSeconds);
    m_heldDrop.update(cpos, deltaSeconds);
    m_catIdInput.update(deltaSeconds);
    m_catNameInput.update(deltaSeconds);
    m_catAcronymInput.update(deltaSeconds);
    m_catAddBtn.update(cpos, deltaSeconds);
    m_catBackBtn.update(cpos, deltaSeconds);
    m_seasonStartInput.update(deltaSeconds);
    m_seasonEndInput.update(deltaSeconds);
    m_seasonPctInput.update(deltaSeconds);
    m_seasonAddBtn.update(cpos, deltaSeconds);
    m_seasonToggleBtn.update(cpos, deltaSeconds);
    m_seasonBackBtn.update(cpos, deltaSeconds);
    m_invSearchInput.update(deltaSeconds);
    m_invQtyInput.update(deltaSeconds);
    m_invSetBtn.update(cpos, deltaSeconds);
    m_invBackBtn.update(cpos, deltaSeconds);
    m_peIdInput.update(deltaSeconds);
    m_peNameInput.update(deltaSeconds);
    m_peAcronymInput.update(deltaSeconds);
    m_pePriceInput.update(deltaSeconds);
    m_peTaxInput.update(deltaSeconds);
    m_peDiscountInput.update(deltaSeconds);
    m_peQtyInput.update(deltaSeconds);
    m_peUnitDrop.update(cpos, deltaSeconds);
    m_peCategoryDrop.update(cpos, deltaSeconds);
    m_peSaveBtn.update(cpos, deltaSeconds);
    m_peBarcodeBtn.update(cpos, deltaSeconds);
    m_peBackBtn.update(cpos, deltaSeconds);
    m_reportDrop.update(cpos, deltaSeconds);
    m_reportFromInput.update(deltaSeconds);
    m_reportToInput.update(deltaSeconds);
    m_reportRunBtn.update(cpos, deltaSeconds);
    m_reportBackBtn.update(cpos, deltaSeconds);
    m_reportPrevBtn.update(cpos, deltaSeconds);
    m_reportNextBtn.update(cpos, deltaSeconds);
    m_heldResumeBtn.update(cpos, deltaSeconds);
    m_heldDeleteBtn.update(cpos, deltaSeconds);
    m_heldBackBtn.update(cpos, deltaSeconds);
    m_heldPrevBtn.update(cpos, deltaSeconds);
    m_heldNextBtn.update(cpos, deltaSeconds);
    m_oeOrderIdInput.update(deltaSeconds);
    m_oeCustomerInput.update(deltaSeconds);
    m_oeContactInput.update(deltaSeconds);
    m_oeLoadBtn.update(cpos, deltaSeconds);
    m_oeApproveBtn.update(cpos, deltaSeconds);
    m_oeSaveBtn.update(cpos, deltaSeconds);
    m_oeBackBtn.update(cpos, deltaSeconds);
    m_printPdfBtn.update(cpos, deltaSeconds);
    m_printCloseBtn.update(cpos, deltaSeconds);
    m_posHoldBtn.setEnabled(!m_orderService.empty());
    m_posResumeBtn.setEnabled(!m_heldIds.empty());
    m_oeApproveBtn.setEnabled(m_role == UserRole::Manager);
    m_posBillBtn.setEnabled(!m_orderService.empty());
    m_posCamBtn.setLabel(m_cameraRunning ? "Stop Cam" : "Start Cam");

    // Digit-tile pop when a new digit appears.
    const std::string code = m_codeInput.getText();
    if (code.size() > m_prevCodeLen && !code.empty())
        m_tilePop[code.size() - 1] = 1.0f;
    m_prevCodeLen = code.size();

    m_generate.setEnabled(isValidCode(code));
    if (isValidCode(code))
    {
        const std::string wanted = "barcode_" + code + ".bmp";
        if (m_fileNameInput.getText().empty() || m_fileNameInput.getText() == m_lastAutoFileName)
        {
            m_fileNameInput.setText(wanted);
            m_lastAutoFileName = wanted;
        }
    }
    m_scanNextBtn.setEnabled(m_scanLocked);

    if (m_view == View::CameraScanner || m_view == View::PosOrder)
    {
        Image localFrame;
        bool frameReady = false;
        std::string newCode;
        bool codeDetected = false;
        {
            std::lock_guard<std::mutex> lock(m_cameraMutex);
            m_cameraStatusUi = m_cameraStatus;
            if (m_newCameraFrameReady)
            {
                localFrame = m_latestCameraFrame;
                frameReady = true;
                m_newCameraFrameReady = false;
            }
            if (m_newCodeScanned)
            {
                newCode = m_scannedCode;
                codeDetected = true;
                m_newCodeScanned = false;
            }
        }
        // Both camera views show the live feed, so frames become a
        // texture in the POS screen too (not just on the Camera page).
        if (frameReady && localFrame.width > 0 && localFrame.height > 0)
            updateCameraTexture(localFrame);
        if (m_view == View::CameraScanner)
        {
            if (codeDetected && !newCode.empty())
                onBarcodeScanned(newCode);
        }
        else if (codeDetected && !newCode.empty())
        {
            // POS: a camera scan fills the barcode box, shows the
            // product, and adds one unit straight to the order - like
            // a supermarket till. Scanning the same item again adds
            // another unit. Then unlock so the next item can be scanned.
            m_posBarcode.setText(newCode);
            posDescribe();
            if (m_descProduct) posAddOne(newCode);
            resetScan();
        }
    }
}

// --------------------------------------------------------------------- draw
void BarcodeStudioGui::draw()
{
    const sf::Vector2f winSize{m_w, m_h};  // layout fills the whole window
    m_window.clear(ui::theme::Canvas);
    ui::theme::drawGradientBackground(m_window, winSize,
        0.5f + 0.5f * std::sin(m_elapsed * 0.6f));
    drawParticles();

    // One single screen filling the whole window: the sidebar (its
    // own texture, so it can slide away) and the current screen.
    m_sidebarTexture.clear(sf::Color::Transparent);
    m_sidebarRt = &m_sidebarTexture;
    drawSidebar();
    m_sidebarTexture.display();
    m_contentTexture.clear(sf::Color::Transparent);
    m_rt = &m_contentTexture;
    if (m_view == View::Generator) drawGenerator();
    else if (m_view == View::FileScanner) drawFileScanner();
    else if (m_view == View::CameraScanner) drawCameraScanner();
    else if (m_view == View::PosOrder) drawPosOrder();
    else if (m_view == View::Products) drawProducts();
    else if (m_view == View::SelfTest) drawSelfTest();
    else if (m_view == View::Categories) drawCategories();
    else if (m_view == View::Seasons) drawSeasons();
    else if (m_view == View::Inventory) drawInventory();
    else if (m_view == View::ProductEdit) drawProductEdit();
    else if (m_view == View::Reports) drawReports();
    else if (m_view == View::HeldOrders) drawHeldOrders();
    else if (m_view == View::OrderEdit) drawOrderEdit();
    else if (m_view == View::BillPrint) drawBillPrint();
    m_contentTexture.display();
    // Place both textures in raw pixel space (they are already at the
    // window's native resolution, so this is a 1:1 blit - crisp).
    const sf::Vector2u winPx = m_window.getSize();
    m_window.setView(sf::View(sf::FloatRect({0.0f, 0.0f},
        {static_cast<float>(winPx.x), static_cast<float>(winPx.y)})));
    sf::Sprite sidebarSprite(m_sidebarTexture.getTexture());
    sidebarSprite.setPosition({-ui::theme::SidebarWidth * m_renderScale * m_sidebarClosed, 0.0f});
    m_window.draw(sidebarSprite);
    sf::Sprite contentSprite(m_contentTexture.getTexture());
    contentSprite.setPosition({-110.0f * m_renderScale * m_sidebarClosed, 0.0f});
    m_window.draw(contentSprite);
    m_window.setView(sf::View(sf::FloatRect({0.0f, 0.0f}, {m_w, m_h})));

    drawSidebarToggle();
    drawRoleButton();
    drawBursts();
    m_window.display();
}

sf::FloatRect BarcodeStudioGui::sidebarToggleRect() const
{
    // Slides with the sidebar: top-right corner while open, top-left
    // corner of the screen once the sidebar is closed.
    return sf::FloatRect({180.0f - 168.0f * m_sidebarClosed, 11.0f}, {34.0f, 32.0f});
}

void BarcodeStudioGui::drawSidebarToggle()
{
    const sf::FloatRect r = sidebarToggleRect();
    ui::theme::drawRoundedBox(m_window, r.position, r.size, 9.0f,
        ui::theme::White, ui::theme::Border, 1.2f);
    const float cx = r.position.x + r.size.x / 2.0f;
    const float cy = r.position.y + r.size.y / 2.0f;
    for (float dy : {-6.0f, 0.0f, 6.0f})
    {
        sf::RectangleShape line({16.0f, 2.4f});
        line.setPosition({cx - 8.0f, cy + dy - 1.2f});
        line.setFillColor(ui::theme::PrimaryPressed);
        m_window.draw(line);
    }
}

void BarcodeStudioGui::drawRoleButton()
{
    const sf::FloatRect r({kLeft, 22.0f}, {150.0f, 32.0f});
    const bool isManager = (m_role == UserRole::Manager);
    ui::theme::drawRoundedBox(rt(), r.position, r.size, 10.0f,
        isManager ? ui::theme::LightPanel : ui::theme::White,
        ui::theme::Border, 1.0f);
    rt().draw(makeText(isManager ? "Manager Mode" : "Cashier Mode",
        14, {r.position.x + 12.0f, r.position.y + 6.0f},
        isManager ? ui::theme::ActiveText : ui::theme::Heading));
}

sf::Text BarcodeStudioGui::makeText(const std::string& value, unsigned int size,
    const sf::Vector2f& position, sf::Color color) const
{
    sf::Text text(m_font, value, ui::theme::scaledSize(size));
    ui::theme::applyTextScale(text);
    text.setPosition(position);
    text.setFillColor(color);
    return text;
}

void BarcodeStudioGui::drawSidebar()
{
    sf::RectangleShape sidebar({ui::theme::SidebarWidth, m_h});
    sidebar.setFillColor(ui::theme::SidebarBg);
    sidebarRt().draw(sidebar);

    sf::Text title = makeText("Chili Mili", 22, {56.0f, 30.0f}, ui::theme::Heading);
    const sf::FloatRect tb = ui::theme::scaledBounds(title);
    ui::theme::drawHeart(sidebarRt(),
        {32.0f, 30.0f + tb.position.y + tb.size.y / 2.0f}, 12.0f, ui::theme::Primary);
    sidebarRt().draw(title);

    // ---- Role switch pill ----
    {
        const sf::FloatRect r({12.0f, 60.0f}, {196.0f, 28.0f});
        const bool isManager = (m_role == UserRole::Manager);
        ui::theme::drawRoundedBox(sidebarRt(), r.position, r.size, 8.0f,
            isManager ? ui::theme::LightPanel : ui::theme::White,
            ui::theme::Border, 1.0f);
        sidebarRt().draw(makeText(isManager ? "Role: Manager" : "Role: Cashier",
            13, {22.0f, 66.0f}, isManager ? ui::theme::ActiveText : ui::theme::Heading));
    }

    // ---- Nav entries: cashier always, manager-only when role is Manager ----
    struct NavEntry { View v; const char* label; bool managerOnly; };
    static const NavEntry kNav[] = {
        {View::Generator,     "Generate",      false},
        {View::FileScanner,   "Read from File",false},
        {View::CameraScanner, "Camera Scan",   false},
        {View::PosOrder,      "POS Order",     false},
        {View::HeldOrders,    "Held Orders",   false},
        {View::Products,      "Products",      false},
        {View::OrderEdit,     "Order Edit",    false},
        {View::Categories,    "Categories",    true},
        {View::Seasons,       "Seasons",       true},
        {View::Inventory,     "Inventory",     true},
        {View::ProductEdit,   "Edit Products", true},
        {View::Reports,       "Reports",       true},
        {View::SelfTest,      "Self-Test",     false},
    };
    const bool isManager = (m_role == UserRole::Manager);
    float y = 100.0f;
    float pillY = 100.0f;
    for (const NavEntry& e : kNav)
    {
        if (e.managerOnly && !isManager) continue;
        if (m_view == e.v) pillY = y;
        y += 34.0f;
    }
    ui::theme::drawRoundedBox(sidebarRt(), {12.0f, pillY}, {196.0f, 30.0f}, 10.0f, ui::theme::White);
    sf::RectangleShape accent({4.0f, 20.0f});
    accent.setPosition({12.0f, pillY + 5.0f});
    accent.setFillColor(ui::theme::Primary);
    sidebarRt().draw(accent);

    y = 100.0f;
    for (const NavEntry& e : kNav)
    {
        if (e.managerOnly && !isManager) continue;
        sidebarRt().draw(makeText(e.label, 14, {30.0f, y + 6.0f},
            m_view == e.v ? ui::theme::Heading : ui::theme::Text));
        y += 34.0f;
    }

    drawMascot();
    ui::theme::drawRoundedBox(sidebarRt(), {12.0f, 606.0f}, {196.0f, 48.0f}, 12.0f,
        ui::theme::White, ui::theme::Accent, 1.2f);
    sidebarRt().draw(makeText("Exit", 16, {30.0f, 619.0f}, ui::theme::PrimaryPressed));
}

void BarcodeStudioGui::drawPreview(const sf::FloatRect& bounds, const sf::Texture& texture, bool has)
{
    // Pure white frame: a barcode must stay black-on-white to remain scannable.
    ui::theme::drawRoundedBox(rt(), bounds.position, bounds.size, 10.0f,
        sf::Color::White, ui::theme::Border, 1.2f);
    if (!has) return;
    const sf::Vector2u size = texture.getSize();
    if (size.x == 0 || size.y == 0) return;
    const float scale = std::min((bounds.size.x - 24.0f) / size.x, (bounds.size.y - 24.0f) / size.y);
    const sf::Vector2f origin{bounds.position.x + (bounds.size.x - size.x * scale) / 2.0f,
        bounds.position.y + (bounds.size.y - size.y * scale) / 2.0f};
    sf::Sprite sprite(texture);
    sprite.setScale({scale, scale});
    sprite.setPosition(origin);
    rt().draw(sprite);
}

// ------------------------------------------------------------------ screens
void BarcodeStudioGui::drawDigitTiles(float shakeX)
{
    const std::string code = m_codeInput.getText();
    const bool allValid = isValidCode(code);
    for (int i = 0; i < 6; ++i)
    {
        const float pop = 1.0f + m_tilePop[static_cast<std::size_t>(i)] * 0.22f;
        const sf::Vector2f size{44.0f * pop, 54.0f * pop};
        const sf::Vector2f centre{272.0f + shakeX + i * 52.0f + 22.0f, 291.0f};
        const sf::Vector2f pos{centre.x - size.x / 2.0f, centre.y - size.y / 2.0f};
        const bool filled = i < static_cast<int>(code.size());
        const sf::Color fill = filled ? ui::theme::LightPanel : ui::theme::White;
        const sf::Color edge = allValid ? ui::theme::Success
            : (filled ? ui::theme::Primary : ui::theme::Border);
        ui::theme::drawRoundedBox(rt(), pos, size, 12.0f, fill, edge, filled ? 1.8f : 1.2f);
        sf::Text t(m_font, filled ? std::string(1, code[static_cast<std::size_t>(i)]) : ".",
            ui::theme::scaledSize(static_cast<unsigned int>(23.0f * pop)));
        ui::theme::applyTextScale(t);
        t.setFillColor(filled ? ui::theme::Heading : ui::theme::Muted);
        const sf::FloatRect b = ui::theme::scaledBounds(t);
        t.setPosition({centre.x - b.size.x / 2.0f - b.position.x,
            centre.y - b.size.y / 2.0f - b.position.y});
        rt().draw(t);
    }
    if (allValid)
        ui::theme::drawSparkle(rt(), {272.0f + 5 * 52.0f + 52.0f, 262.0f},
            7.0f + 2.0f * std::sin(m_elapsed * 5.0f), ui::theme::Success);
}

void BarcodeStudioGui::drawGenerator()
{
    rt().draw(makeText("Barcode Generator", 25, {kLeft, 26.0f}, ui::theme::Heading));
    const float rx = m_w - 1000.0f;  // extra width when the window is wide
    const sf::FloatRect left({kLeft, 102.0f}, {360.0f, 522.0f});
    const sf::FloatRect right({622.0f, 102.0f}, {352.0f + rx, 522.0f});
    ui::theme::drawCard(rt(), left);
    ui::theme::drawCard(rt(), right);

    const float shakeX = m_shake > 0.0f
        ? std::sin(m_shake * 55.0f) * 7.0f * (m_shake / 0.4f) : 0.0f;

    rt().draw(makeText("Code input", 17, {272.0f, 126.0f}, ui::theme::Heading));
    if (shakeX != 0.0f) m_codeInput.setPosition({272.0f + shakeX, 182.0f});
    m_codeInput.draw(rt());
    if (shakeX != 0.0f) m_codeInput.setPosition({272.0f, 182.0f});

    const std::string code = m_codeInput.getText();
    if (code.empty())
        rt().draw(makeText("Start typing - each digit pops into a tile below", 12,
            {272.0f, 238.0f}, ui::theme::Muted));
    else if (isValidCode(code))
        rt().draw(makeText("Valid code - ready to generate!", 12, {272.0f, 238.0f}, ui::theme::Success));
    else
        rt().draw(makeText("Enter exactly 6 digits, first digit cannot be 0  (" +
            std::to_string(code.size()) + " / 6)", 12, {272.0f, 238.0f}, ui::theme::Error));
    drawDigitTiles(shakeX);
    m_generateRandom.draw(rt());

    rt().draw(makeText("Save as (BMP file name)", 14, {272.0f, 402.0f}, ui::theme::Heading));
    m_fileNameInput.draw(rt());
    if (isValidCode(code))  // pulsing glow behind the armed Generate button
    {
        const std::uint8_t a = static_cast<std::uint8_t>(55.0f + 40.0f * std::sin(m_elapsed * 4.0f));
        ui::theme::drawRoundedBox(rt(), {264.0f, 484.0f}, {324.0f, 64.0f}, 16.0f,
            sf::Color(255, 92, 138, a));
    }
    m_generate.draw(rt());

    rt().draw(makeText("Live preview", 17, {648.0f, 126.0f}, ui::theme::Heading));
    drawPreview(sf::FloatRect({642.0f, 162.0f}, {312.0f + rx, 300.0f}), m_previewTexture, m_hasPreview);
    if (!m_hasPreview)
        rt().draw(makeText("Your barcode will\nappear here", 14, {716.0f, 292.0f}, ui::theme::Muted));
    if (m_hasPreview)
    {
        rt().draw(makeText(shorten(m_previewPath, 44), 12, {648.0f, 478.0f}, ui::theme::Muted));
        rt().draw(makeText("Dimensions: " + std::to_string(m_previewTexture.getSize().x) + " x " +
            std::to_string(m_previewTexture.getSize().y), 12, {648.0f, 500.0f}, ui::theme::Muted));
        ui::theme::drawStatus(rt(), m_font, {648.0f, 532.0f}, {150.0f, 32.0f}, "Saved & Ready", true);
    }
}

void BarcodeStudioGui::drawFileScanner()
{
    rt().draw(makeText("Read Barcode from File", 25, {kLeft, 26.0f}, ui::theme::Heading));
    const float rx = m_w - 1000.0f;
    ui::theme::drawCard(rt(), {{kLeft, 102.0f}, {728.0f + rx, 168.0f}});
    m_pathInput.setSize({452.0f + rx, 42.0f});
    m_browse.setPosition({736.0f + rx, 146.0f});
    rt().draw(makeText("BMP file path", 14, {272.0f, 120.0f}, ui::theme::Heading));
    m_pathInput.draw(rt());
    m_browse.draw(rt());
    m_decode.draw(rt());
    rt().draw(makeText("Tip: typing or pasting a path works too - quotes are stripped automatically.",
        12, {492.0f, 220.0f}, ui::theme::Muted));

    ui::theme::drawCard(rt(), {{kLeft, 286.0f}, {356.0f, 338.0f}});
    ui::theme::drawCard(rt(), {{618.0f, 286.0f}, {356.0f + rx, 338.0f}});
    rt().draw(makeText("Loaded image", 17, {272.0f, 310.0f}, ui::theme::Heading));
    drawPreview(sf::FloatRect({262.0f, 342.0f}, {324.0f, 220.0f}), m_previewTexture, m_hasPreview);
    if (!m_hasPreview)
        rt().draw(makeText("No image loaded yet", 13, {330.0f, 442.0f}, ui::theme::Muted));
    if (m_loadedImage.width > 0)
        rt().draw(makeText("Resolution: " + std::to_string(m_loadedImage.width) + " x " +
            std::to_string(m_loadedImage.height), 12, {272.0f, 578.0f}, ui::theme::Muted));

    rt().draw(makeText("Decoded output", 17, {644.0f, 310.0f}, ui::theme::Heading));
    if (!m_decodeAttempted)
    {
        rt().draw(makeText("Nothing decoded yet.\nChoose a BMP and press Decode.", 15,
            {644.0f, 370.0f}, ui::theme::Muted));
    }
    else if (m_decodeOk)
    {
        rt().draw(makeText(m_decoded, 34, {644.0f, 366.0f}, ui::theme::ActiveText));
        ui::theme::drawStatus(rt(), m_font, {644.0f, 436.0f}, {160.0f, 32.0f}, "Decode Success", true);
        if (m_descProduct)
        {
            rt().draw(makeText("Product: " + m_descProduct->name + "  -  PKR " +
                stringutils::money(m_descProduct->unitPrice) + "  (stock " +
                std::to_string(m_descProduct->availableQuantity) + ")",
                12, {644.0f, 486.0f}, ui::theme::ActiveText));
            m_fileAddBtn.draw(rt());
        }
        else if (m_decodeOk)
            rt().draw(makeText("No product found for barcode " + m_decoded +
                " in Data/product.txt.",
                12, {644.0f, 486.0f}, ui::theme::Error));
    }
    else
    {
        rt().draw(makeText("No barcode found\nin this image.", 19, {644.0f, 360.0f}, ui::theme::Error));
        ui::theme::drawStatus(rt(), m_font, {644.0f, 436.0f}, {150.0f, 32.0f}, "Decode Failed", false);
    }
}

void BarcodeStudioGui::drawCameraScanner()
{
    rt().draw(makeText("Camera Scan", 25, {kLeft, 22.0f}, ui::theme::Heading));
    m_cameraToggleBtn.draw(rt());
    m_scanNextBtn.draw(rt());

    const float rx = m_w - 1000.0f;
    const sf::FloatRect leftCard({kLeft, 132.0f}, {424.0f, 492.0f});
    const sf::FloatRect rightCard({686.0f, 132.0f}, {288.0f + rx, 492.0f});
    ui::theme::drawCard(rt(), leftCard);
    ui::theme::drawCard(rt(), rightCard);

    rt().draw(makeText("Live Camera Feed", 17, {266.0f, 148.0f}, ui::theme::Heading));
    const sf::FloatRect video({264.0f, 182.0f}, {388.0f, 280.0f});
    sf::RectangleShape videoBg(video.size);
    videoBg.setPosition(video.position);
    videoBg.setFillColor(sf::Color(24, 16, 22));
    rt().draw(videoBg);
    if (m_hasCameraFrame)
    {
        sf::Sprite camSprite(m_cameraTexture);
        const sf::Vector2u s = m_cameraTexture.getSize();
        if (s.x > 0 && s.y > 0)
        {
            const float scale = std::min(video.size.x / s.x, video.size.y / s.y);
            camSprite.setScale({scale, scale});
            camSprite.setPosition({video.position.x + (video.size.x - s.x * scale) / 2.0f,
                video.position.y + (video.size.y - s.y * scale) / 2.0f});
            rt().draw(camSprite);
        }
    }
    else
    {
        rt().draw(makeText(m_cameraRunning ? "Connecting to webcam..." : "Press Start Camera to begin",
            14, {330.0f, 312.0f}, ui::theme::Muted));
    }
    rt().draw(makeText("Camera: " + m_cameraStatusUi, 13, {266.0f, 478.0f}, ui::theme::ActiveText));

    rt().draw(makeText("Detection & Output", 17, {706.0f, 148.0f}, ui::theme::Heading));
    if (m_hasGeneratedBarcode)
    {
        rt().draw(makeText("Decoded number:", 13, {706.0f, 190.0f}, ui::theme::Muted));
        rt().draw(makeText(m_decoded, 36, {706.0f, 212.0f}, ui::theme::ActiveText));
        rt().draw(makeText("Generated ITF barcode:", 13, {706.0f, 272.0f}, ui::theme::Muted));
        drawPreview(sf::FloatRect({706.0f, 298.0f}, {248.0f + rx, 128.0f}), m_generatedBarcodeTexture, true);
        rt().draw(makeText(shorten(m_generatedBarcodePath, 34), 11, {706.0f, 442.0f}, ui::theme::Success));
        ui::theme::drawStatus(rt(), m_font, {706.0f, 472.0f}, {190.0f, 30.0f}, "Decoded & Generated", true);
        if (m_descProduct)
        {
            rt().draw(makeText("Product: " + shorten(m_descProduct->name, 24) + "\nPKR " +
                stringutils::money(m_descProduct->unitPrice) + " - stock " +
                std::to_string(m_descProduct->availableQuantity),
                12, {706.0f, 516.0f}, ui::theme::ActiveText));
            m_camAddBtn.draw(rt());
        }
        else
            rt().draw(makeText("No product found for barcode " + m_decoded +
                "\nNot in Data/product.txt - add the product first.",
                12, {706.0f, 516.0f}, ui::theme::Error));
    }
    else
    {
        ui::theme::drawRoundedBox(rt(), {706.0f, 190.0f}, {248.0f + rx, 230.0f}, 12.0f, ui::theme::LightPanel);
        rt().draw(makeText("Waiting for barcode...", 16, {726.0f, 292.0f}, ui::theme::ActiveText));
    }
}

void BarcodeStudioGui::drawSelfTest()
{
    rt().draw(makeText("Self-Test", 25, {kLeft, 26.0f}, ui::theme::Heading));
    const float rx = m_w - 1000.0f;
    ui::theme::drawCard(rt(), {{kLeft, 102.0f}, {728.0f + rx, 522.0f}});
    m_runTest.draw(rt());
    const float totalTime = static_cast<float>(m_testRows.size()) * 0.12f + 0.35f;
    const float overall = m_testRows.empty() ? 0.0f : std::min(1.0f, m_testRunTime / totalTime);
    if (!m_testRows.empty())
    {
        const int shownPassed = static_cast<int>(m_testPassed * overall + 0.5f);
        const bool allOk = m_testPassed == static_cast<int>(m_testRows.size());
        ui::theme::drawStatus(rt(), m_font, {742.0f + rx, 132.0f}, {206.0f, 32.0f},
            "Result: " + std::to_string(shownPassed) + " / " +
            std::to_string(m_testRows.size()) + " passed", allOk && overall >= 1.0f);
        // Animated progress bar
        ui::theme::drawRoundedBox(rt(), {272.0f, 232.0f}, {676.0f + rx, 9.0f}, 4.5f, ui::theme::LightPanel);
        if (overall > 0.0f)
            ui::theme::drawRoundedBox(rt(), {272.0f, 232.0f}, {(676.0f + rx) * overall, 9.0f}, 4.5f,
                allOk ? ui::theme::Success : ui::theme::Primary);
    }
    rt().draw(makeText("Each sample is encoded with renderBarcode() and then decoded\nwith decodeBarcode() - the result must match the original code.",
        13, {272.0f, 188.0f}, ui::theme::Muted));

    if (m_testRows.empty())
    {
        rt().draw(makeText("Press Run Self-Test to check the whole pipeline.", 15,
            {330.0f, 360.0f}, ui::theme::Muted));
        return;
    }
    float y = 256.0f;
    for (std::size_t i = 0; i < m_testRows.size(); ++i)
    {
        const TestRow& row = m_testRows[i];
        const float p = std::min(1.0f, std::max(0.0f, (m_testRunTime - static_cast<float>(i) * 0.12f) / 0.3f));
        if (p > 0.0f)
        {
            const float slide = (1.0f - easeOut(p)) * 34.0f;
            ui::theme::drawRoundedBox(rt(), {272.0f + slide, y}, {676.0f + rx, 48.0f}, 10.0f, ui::theme::LightPanel);
            rt().draw(makeText(row.code, 16, {292.0f + slide, y + 14.0f}, ui::theme::Heading));
            rt().draw(makeText("->", 16, {380.0f + slide, y + 14.0f}, ui::theme::Muted));
            rt().draw(makeText(row.result.empty() ? "(nothing)" : row.result, 16,
                {430.0f + slide, y + 14.0f}, ui::theme::Text));
            if (p > 0.6f)
                ui::theme::drawStatus(rt(), m_font, {838.0f + rx, y + 9.0f}, {90.0f, 30.0f},
                    row.ok ? "PASS" : "FAIL", row.ok);
        }
        y += 58.0f;
    }
}

// ------------------------------------------------------------------ actions
void BarcodeStudioGui::setPreviewFromImage(const Image& image)
{
    if (image.width <= 0 || image.height <= 0 || image.pixels.empty()) { m_hasPreview = false; return; }
    sf::Image sfImage({static_cast<unsigned int>(image.width),
        static_cast<unsigned int>(image.height)}, sf::Color::White);
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x)
        {
            const std::uint8_t v = image.pixels[static_cast<std::size_t>(y) * image.width + x];
            sfImage.setPixel({static_cast<unsigned int>(x), static_cast<unsigned int>(y)},
                sf::Color(v, v, v));
        }
    m_hasPreview = m_previewTexture.loadFromImage(sfImage);
    m_previewTexture.setSmooth(false);
}

void BarcodeStudioGui::generateBarcode()
{
    const std::string code = m_codeInput.getText();
    if (!isValidCode(code))
    {
        m_shake = 0.4f;
        showToast("Enter exactly 6 digits (first digit 1-9).", false);
        return;
    }
    std::string path = trimmed(m_fileNameInput.getText());
    if (path.empty()) path = "barcode_" + code + ".bmp";
    // Only the file NAME is taken from the box: generated barcodes
    // always live in the project's barcodes/ folder.
    const std::size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) path = path.substr(slash + 1);
    if (path.size() < 4 || path.substr(path.size() - 4) != ".bmp") path += ".bmp";
    path = barcodesDir() + "/" + path;
    const Image image = renderBarcode(code, 3, 120);
    if (image.width <= 0 || !saveBMP(image, path))
    {
        showToast("Could not save the barcode BMP.", false);
        return;
    }
    m_previewPath = path;
    setPreviewFromImage(image);
    celebrate("Yay! Barcode\ncreated!", {798.0f, 310.0f});
    showToast("Barcode saved: " + path, true);
}

std::string BarcodeStudioGui::stripQuotes(std::string s)
{
    s = trimmed(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return trimmed(s);
}

void BarcodeStudioGui::browseFile()
{
    const std::string path = chooseFile();
    if (!path.empty()) m_pathInput.setText(path);
}

void BarcodeStudioGui::decodeFile()
{
    const std::string path = stripQuotes(m_pathInput.getText());
    m_pathInput.setText(path);
    m_decodeAttempted = true;
    if (path.empty())
    {
        m_decodeOk = false;
        m_decoded.clear();
        showToast("Enter a BMP file path first.", false);
        return;
    }
    bool loaded = loadBMP(path, m_loadedImage);
    if (!loaded && path.find_first_of("/\\") == std::string::npos)
        loaded = loadBMP(barcodesDir() + "/" + path, m_loadedImage);
    if (!loaded)
    {
        m_decodeOk = false;
        m_decoded.clear();
        m_hasPreview = false;
        showToast("Could not load this BMP file.", false);
        return;
    }
    setPreviewFromImage(m_loadedImage);
    m_decodeOk = decodeBarcode(m_loadedImage, m_decoded);
    if (m_decodeOk) m_descProduct = m_productService.findByBarcode(m_decoded);
    if (!m_decodeOk) m_decoded.clear();
    else celebrate("I read it!\nIt's " + m_decoded + "!", {790.0f, 380.0f});
    showToast(m_decodeOk ? "Barcode decoded successfully." : "No barcode found in this image.",
        m_decodeOk);
}

void BarcodeStudioGui::runSelfTest()
{
    const std::array<std::string, 6> samples = {"123456", "987654", "100000", "200001", "555555", "999999"};
    m_testRows.clear();
    m_testPassed = 0;
    for (const std::string& code : samples)
    {
        std::string result;
        const Image img = renderBarcode(code, 3, 120);
        const bool ok = decodeBarcode(img, result) && result == code;
        m_testRows.push_back({code, result, ok});
        if (ok) ++m_testPassed;
    }
    m_testRunTime = 0.0f;  // restart the staggered row animation
    if (m_testPassed == 6) celebrate("Perfect score!\n6 out of 6!", {610.0f, 330.0f});
    showToast("Self-test: " + std::to_string(m_testPassed) + " / 6 passed.",
        m_testPassed == 6);
}

void BarcodeStudioGui::showToast(const std::string& /*message*/, bool /*success*/)
{
    // No pop-up toast box (user feedback): results are reported inside
    // the screens themselves and by the mascot, not in a corner box.
}

std::string BarcodeStudioGui::chooseFile()
{
#ifdef _WIN32
    char fileName[MAX_PATH] = {};
    OPENFILENAMEA dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = "Bitmap files (*.bmp)\0*.bmp\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    const std::string initialDir = std::filesystem::absolute(barcodesDir()).string();
    dialog.lpstrInitialDir = initialDir.c_str();
    if (GetOpenFileNameA(&dialog)) return fileName;
#endif
    return {};
}

// ------------------------------------------------------------------- camera
void BarcodeStudioGui::startCamera()
{
    if (m_cameraRunning) return;
    if (m_cameraThread.joinable()) m_cameraThread.join();
    m_cameraAttempts = 0;
    resetScanState();
    m_cameraRunning = true;
    m_cameraPaused = false;
    {
        std::lock_guard<std::mutex> lock(m_cameraMutex);
        m_cameraStatus = "Starting camera...";
    }
    m_cameraToggleBtn.setLabel("Stop Camera");
    m_mascotText = "Watching...\nhold it steady!";
    m_cameraThread = std::thread(&BarcodeStudioGui::cameraWorkerLoop, this);
}

void BarcodeStudioGui::stopCamera()
{
    if (!m_cameraRunning) return;
    m_cameraRunning = false;
    if (m_cameraThread.joinable()) m_cameraThread.join();
    m_cameraPaused = false;
    m_hasCameraFrame = false;
    {
        std::lock_guard<std::mutex> lock(m_cameraMutex);
        m_cameraStatus = "Camera stopped";
        m_cameraStatusUi = m_cameraStatus;
    }
    m_cameraToggleBtn.setLabel("Start Camera");
}

void BarcodeStudioGui::cameraWorkerLoop()
{
    Camera camera;
    if (!camera.initialize())
    {
        std::lock_guard<std::mutex> lock(m_cameraMutex);
        m_cameraStatus = "Camera initialization failed";
        m_cameraRunning = false;
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_cameraMutex);
        m_cameraStatus = "Live - scanning";
    }

    std::string candidate;
    int candidateCount = 0;
    int seenGeneration = m_scanGeneration.load();
    auto lastScan = std::chrono::steady_clock::now();

    while (m_cameraRunning)
    {
        // A reset (new order, bill closed, camera restarted) bumps the
        // generation: drop any half-built candidate so the next scan
        // needs a fresh set of consistent decodes.
        const int gen = m_scanGeneration.load();
        if (gen != seenGeneration)
        {
            seenGeneration = gen;
            candidate.clear();
            candidateCount = 0;
        }
        if (m_cameraPaused)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            continue;
        }
        Image frame;
        if (!camera.captureFrame(frame))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            continue;
        }
        std::string detected;
        bool found = false;
        const auto now = std::chrono::steady_clock::now();
        if (!m_scanLocked &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastScan).count() >= 80)
        {
            lastScan = now;
            ++m_cameraAttempts;
            found = decodeBarcode(frame, detected);
            if (found && !detected.empty())
            {
                if (detected == candidate) ++candidateCount;
                else { candidate = detected; candidateCount = 1; }
            }
            else
            {
                candidate.clear();
                candidateCount = 0;
            }
            m_consistentCount = candidateCount;
        }
        {
            std::lock_guard<std::mutex> lock(m_cameraMutex);
            m_latestCameraFrame = frame;
            m_newCameraFrameReady = true;
            if (candidateCount >= 3 && !candidate.empty() && !m_scanLocked)
            {
                m_scannedCode = candidate;
                m_newCodeScanned = true;
                m_scanLocked = true;
            }
        }
    }
    camera.cleanup();
}

void BarcodeStudioGui::updateCameraTexture(const Image& frame)
{
    const std::size_t count = static_cast<std::size_t>(frame.width) * frame.height;
    if (frame.pixels.size() < count) return;
    if (m_cameraRgba.size() != count * 4) m_cameraRgba.resize(count * 4);
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::uint8_t g = frame.pixels[i];
        m_cameraRgba[i * 4 + 0] = g;
        m_cameraRgba[i * 4 + 1] = g;
        m_cameraRgba[i * 4 + 2] = g;
        m_cameraRgba[i * 4 + 3] = 255;
    }
    const sf::Vector2u size(static_cast<unsigned int>(frame.width),
        static_cast<unsigned int>(frame.height));
    if (m_cameraTexture.getSize() != size) (void)m_cameraTexture.resize(size);
    m_cameraTexture.update(m_cameraRgba.data());
    m_hasCameraFrame = true;
}

void BarcodeStudioGui::onBarcodeScanned(const std::string& code)
{
    m_decoded = code;
    const Image generated = renderBarcode(code, 3, 120);
    m_generatedBarcodePath = barcodesDir() + "/barcode_" + code + ".bmp";
    saveBMP(generated, m_generatedBarcodePath);
    sf::Image sfImg({static_cast<unsigned int>(generated.width),
        static_cast<unsigned int>(generated.height)}, sf::Color::White);
    for (int y = 0; y < generated.height; ++y)
        for (int x = 0; x < generated.width; ++x)
        {
            const std::uint8_t v = generated.pixels[static_cast<std::size_t>(y) * generated.width + x];
            sfImg.setPixel({static_cast<unsigned int>(x), static_cast<unsigned int>(y)},
                sf::Color(v, v, v));
        }
    m_descProduct = m_productService.findByBarcode(code);
    m_hasGeneratedBarcode = m_generatedBarcodeTexture.loadFromImage(sfImg);
    m_generatedBarcodeTexture.setSmooth(false);
    celebrate(m_descProduct ? "Got it! " + code + "\nLocked in!"
                            : "Scanned " + code + " -\nno product found!",
        {830.0f, 330.0f});
    showToast("Barcode " + code + " detected & generated!", true);
}

void BarcodeStudioGui::resetScanState()
{
    // Forget everything about the scan in progress: unlock, drop any
    // code that was detected but not yet picked up, and bump the
    // generation so the camera worker drops its current candidate.
    // Called at every order boundary (bill generated/closed, New
    // Order, camera start) - a stale candidate or lock left over
    // from the previous order used to block the next order's scans.
    ++m_scanGeneration;
    m_scanLocked = false;
    m_consistentCount = 0;
    std::lock_guard<std::mutex> lock(m_cameraMutex);
    m_newCodeScanned = false;
    m_scannedCode.clear();
}

// ============================================================ PHASE 3 SCREENS
// Manager screens (Categories, Seasons, Inventory, ProductEdit, Reports)
// and the cashier/manager screens (HeldOrders, OrderEdit, BillPrint).
// Every one of these only READS from caches and CALLS the services; no
// file IO and no rule lives here (SRP).

void BarcodeStudioGui::drawManagerPill(const sf::FloatRect& bounds,
    const std::string& text, bool ok)
{
    ui::theme::drawStatus(rt(), m_font, bounds.position, bounds.size, text, ok);
}

void BarcodeStudioGui::drawCategories()
{
    const float rx = m_w - 1000.0f;
    rt().draw(makeText("Categories", 25, {kLeft, 26.0f}, ui::theme::Heading));
    m_catBackBtn.draw(rt());

    ui::theme::drawCard(rt(), {{246.0f, 92.0f}, {728.0f + rx, 178.0f}});
    rt().draw(makeText("New Category", 15, {272.0f, 108.0f}, ui::theme::Heading));
    rt().draw(makeText("Id (2 digits)", 11, {272.0f, 132.0f}, ui::theme::Muted));
    rt().draw(makeText("Name", 11, {344.0f, 132.0f}, ui::theme::Muted));
    rt().draw(makeText("Acronym", 11, {596.0f, 132.0f}, ui::theme::Muted));
    m_catIdInput.draw(rt());
    m_catNameInput.draw(rt());
    m_catAcronymInput.draw(rt());
    m_catAddBtn.draw(rt());
    if (!m_catMessage.empty())
        rt().draw(makeText(m_catMessage, 12, {272.0f, 256.0f},
            m_catMessage.find("added") != std::string::npos ? ui::theme::Success : ui::theme::Error));

    ui::theme::drawCard(rt(), {{246.0f, 300.0f}, {728.0f + rx, 324.0f}});
    rt().draw(makeText("Existing Categories", 15, {272.0f, 316.0f}, ui::theme::Heading));
    rt().draw(makeText("Id", 11, {272.0f, 344.0f}, ui::theme::Muted));
    rt().draw(makeText("Name", 11, {336.0f, 344.0f}, ui::theme::Muted));
    rt().draw(makeText("Acronym", 11, {596.0f, 344.0f}, ui::theme::Muted));
    float y = 368.0f;
    if (m_categoriesCache.empty())
        rt().draw(makeText("No categories yet - add one above.", 13, {272.0f, 380.0f}, ui::theme::Muted));
    for (const Category& c : m_categoriesCache)
    {
        ui::theme::drawRoundedBox(rt(), {262.0f, y}, {696.0f + rx, 36.0f}, 8.0f, ui::theme::LightPanel);
        rt().draw(makeText(c.id, 13, {272.0f, y + 8.0f}, ui::theme::Heading));
        rt().draw(makeText(c.name, 13, {336.0f, y + 8.0f}, ui::theme::Text));
        rt().draw(makeText(c.acronym, 13, {596.0f, y + 8.0f}, ui::theme::Text));
        y += 40.0f;
    }
}

void BarcodeStudioGui::drawSeasons()
{
    const float rx = m_w - 1000.0f;
    rt().draw(makeText("Seasons", 25, {kLeft, 26.0f}, ui::theme::Heading));
    m_seasonBackBtn.draw(rt());

    ui::theme::drawCard(rt(), {{246.0f, 92.0f}, {728.0f + rx, 178.0f}});
    rt().draw(makeText("New Season", 15, {272.0f, 108.0f}, ui::theme::Heading));
    rt().draw(makeText("Start", 11, {272.0f, 132.0f}, ui::theme::Muted));
    rt().draw(makeText("End", 11, {426.0f, 132.0f}, ui::theme::Muted));
    rt().draw(makeText("Percent (0-100)", 11, {580.0f, 132.0f}, ui::theme::Muted));
    m_seasonStartInput.draw(rt());
    m_seasonEndInput.draw(rt());
    m_seasonPctInput.draw(rt());
    m_seasonAddBtn.draw(rt());
    m_seasonToggleBtn.draw(rt());
    if (!m_seasonMessage.empty())
        rt().draw(makeText(m_seasonMessage, 12, {272.0f, 256.0f},
            m_seasonMessage.find("success") != std::string::npos ? ui::theme::Success : ui::theme::Error));

    ui::theme::drawCard(rt(), {{246.0f, 300.0f}, {728.0f + rx, 324.0f}});
    rt().draw(makeText("Existing Seasons", 15, {272.0f, 316.0f}, ui::theme::Heading));
    rt().draw(makeText("Start", 11, {272.0f, 344.0f}, ui::theme::Muted));
    rt().draw(makeText("End", 11, {396.0f, 344.0f}, ui::theme::Muted));
    rt().draw(makeText("Percent", 11, {520.0f, 344.0f}, ui::theme::Muted));
    rt().draw(makeText("Active", 11, {640.0f, 344.0f}, ui::theme::Muted));
    float y = 368.0f;
    if (m_seasonsCache.empty())
        rt().draw(makeText("No seasons yet - add one above.", 13, {272.0f, 380.0f}, ui::theme::Muted));
    for (std::size_t i = 0; i < m_seasonsCache.size() && i < 6; ++i)
    {
        const Season& s = m_seasonsCache[i];
        const bool selected = (static_cast<int>(i) == m_seasonSelectedIdx);
        ui::theme::drawRoundedBox(rt(), {262.0f, y}, {696.0f + rx, 36.0f}, 8.0f,
            selected ? ui::theme::Active : ui::theme::LightPanel);
        rt().draw(makeText(s.startDate, 13, {272.0f, y + 8.0f}, ui::theme::Text));
        rt().draw(makeText(s.endDate, 13, {396.0f, y + 8.0f}, ui::theme::Text));
        rt().draw(makeText(stringutils::money(s.discountPercent) + "%",
            13, {520.0f, y + 8.0f}, ui::theme::Text));
        rt().draw(makeText(s.isActive ? "true" : "false", 13,
            {640.0f, y + 8.0f}, s.isActive ? ui::theme::Success : ui::theme::Muted));
        y += 40.0f;
    }
}

void BarcodeStudioGui::drawInventory()
{
    const float rx = m_w - 1000.0f;
    rt().draw(makeText("Inventory", 25, {kLeft, 26.0f}, ui::theme::Heading));
    m_invBackBtn.draw(rt());

    ui::theme::drawCard(rt(), {{246.0f, 92.0f}, {728.0f + rx, 108.0f}});
    rt().draw(makeText("Filter / Set Stock", 15, {272.0f, 108.0f}, ui::theme::Heading));
    m_invSearchInput.draw(rt());
    m_invQtyInput.draw(rt());
    m_invSetBtn.draw(rt());
    if (!m_invMessage.empty())
        rt().draw(makeText(m_invMessage, 12, {272.0f, 156.0f},
            m_invMessage.find("updated") != std::string::npos ? ui::theme::Success : ui::theme::Error));

    ui::theme::drawCard(rt(), {{246.0f, 214.0f}, {728.0f + rx, 410.0f}});
    rt().draw(makeText("Products", 15, {272.0f, 230.0f}, ui::theme::Heading));
    rt().draw(makeText("Acronym", 11, {272.0f, 258.0f}, ui::theme::Muted));
    rt().draw(makeText("Name", 11, {366.0f, 258.0f}, ui::theme::Muted));
    rt().draw(makeText("Stock", 11, {676.0f, 258.0f}, ui::theme::Muted));
    const std::string filter = trimStr(m_invSearchInput.getText());
    int shown = 0;
    for (std::size_t i = 0; i < m_productsCache.size() && shown < 8; ++i)
    {
        const Product& p = m_productsCache[i];
        if (!filter.empty()
            && p.acronym.find(filter) == std::string::npos
            && p.name.find(filter) == std::string::npos) continue;
        const float y = 284.0f + static_cast<float>(shown) * 44.0f;
        const bool selected = (static_cast<int>(i) == m_invSelectedIdx);
        ui::theme::drawRoundedBox(rt(), {262.0f, y}, {696.0f + rx, 40.0f}, 8.0f,
            selected ? ui::theme::Active : ui::theme::LightPanel);
        rt().draw(makeText(p.acronym, 13, {272.0f, y + 10.0f}, ui::theme::Heading));
        rt().draw(makeText(truncFront(p.name, 32), 13, {366.0f, y + 10.0f}, ui::theme::Text));
        rt().draw(makeText(std::to_string(p.availableQuantity), 13, {676.0f, y + 10.0f},
            p.availableQuantity > 0 ? ui::theme::Text : ui::theme::Error));
        ++shown;
    }
    if (shown == 0)
        rt().draw(makeText("No products match the filter.", 13, {272.0f, 300.0f}, ui::theme::Muted));
}

void BarcodeStudioGui::drawProductEdit()
{
    const float rx = m_w - 1000.0f;
    rt().draw(makeText("Add / Edit Product", 25, {kLeft, 26.0f}, ui::theme::Heading));
    m_peBackBtn.draw(rt());

    ui::theme::drawCard(rt(), {{246.0f, 92.0f}, {728.0f + rx, 300.0f}});
    rt().draw(makeText("Product", 15, {272.0f, 104.0f}, ui::theme::Heading));
    rt().draw(makeText("Id", 11, {272.0f, 122.0f}, ui::theme::Muted));
    rt().draw(makeText("Name", 11, {364.0f, 122.0f}, ui::theme::Muted));
    rt().draw(makeText("Acronym", 11, {636.0f, 122.0f}, ui::theme::Muted));
    m_peIdInput.draw(rt());
    m_peNameInput.draw(rt());
    m_peAcronymInput.draw(rt());
    rt().draw(makeText("Price", 11, {272.0f, 180.0f}, ui::theme::Muted));
    rt().draw(makeText("Tax", 11, {404.0f, 180.0f}, ui::theme::Muted));
    rt().draw(makeText("Discount", 11, {536.0f, 180.0f}, ui::theme::Muted));
    rt().draw(makeText("Stock", 11, {668.0f, 180.0f}, ui::theme::Muted));
    m_pePriceInput.draw(rt());
    m_peTaxInput.draw(rt());
    m_peDiscountInput.draw(rt());
    m_peQtyInput.draw(rt());
    rt().draw(makeText("Unit", 11, {272.0f, 238.0f}, ui::theme::Muted));
    rt().draw(makeText("Category", 11, {424.0f, 238.0f}, ui::theme::Muted));
    m_peUnitDrop.draw(rt());
    m_peCategoryDrop.draw(rt());
    m_peSaveBtn.draw(rt());
    m_peBarcodeBtn.draw(rt());
    if (!m_peMessage.empty())
        rt().draw(makeText(m_peMessage, 12, {272.0f, 372.0f},
            m_peMessage.find("saved") != std::string::npos
                || m_peMessage.find("OK") != std::string::npos
                ? ui::theme::Success : ui::theme::Error));

    ui::theme::drawCard(rt(), {{246.0f, 396.0f}, {728.0f + rx, 228.0f}});
    rt().draw(makeText("Products (click to load into the form)", 15,
        {272.0f, 412.0f}, ui::theme::Heading));
    rt().draw(makeText("Id", 11, {272.0f, 440.0f}, ui::theme::Muted));
    rt().draw(makeText("Acronym", 11, {336.0f, 440.0f}, ui::theme::Muted));
    rt().draw(makeText("Name", 11, {436.0f, 440.0f}, ui::theme::Muted));
    rt().draw(makeText("Barcode", 11, {676.0f, 440.0f}, ui::theme::Muted));
    float y = 464.0f;
    int shown = 0;
    for (const Product& p : m_productsCache)
    {
        if (shown >= 4) break;
        ui::theme::drawRoundedBox(rt(), {262.0f, y}, {696.0f + rx, 36.0f}, 8.0f, ui::theme::LightPanel);
        rt().draw(makeText(std::to_string(p.id), 13, {272.0f, y + 8.0f}, ui::theme::Text));
        rt().draw(makeText(p.acronym, 13, {336.0f, y + 8.0f}, ui::theme::Heading));
        rt().draw(makeText(truncFront(p.name, 28), 13, {436.0f, y + 8.0f}, ui::theme::Text));
        rt().draw(makeText(p.barcode, 13, {676.0f, y + 8.0f}, ui::theme::Text));
        y += 40.0f;
        ++shown;
    }
    if (m_productsCache.size() > 4)
        rt().draw(makeText("...and " + std::to_string(m_productsCache.size() - 4) +
            " more - use Inventory to browse.", 11, {272.0f, 600.0f}, ui::theme::Muted));

    m_peUnitDrop.drawList(rt());
    m_peCategoryDrop.drawList(rt());
}

void BarcodeStudioGui::drawReports()
{
    const float rx = m_w - 1000.0f;
    rt().draw(makeText("Reports", 25, {kLeft, 26.0f}, ui::theme::Heading));
    m_reportBackBtn.draw(rt());

    ui::theme::drawCard(rt(), {{246.0f, 92.0f}, {728.0f + rx, 76.0f}});
    rt().draw(makeText("Report", 15, {272.0f, 104.0f}, ui::theme::Heading));
    rt().draw(makeText("Custom From", 11, {584.0f, 104.0f}, ui::theme::Muted));
    rt().draw(makeText("Custom To", 11, {736.0f, 104.0f}, ui::theme::Muted));
    m_reportDrop.draw(rt());
    m_reportFromInput.draw(rt());
    m_reportToInput.draw(rt());
    m_reportRunBtn.draw(rt());
    m_reportDrop.drawList(rt());

    ui::theme::drawCard(rt(), {{246.0f, 180.0f}, {728.0f + rx, 420.0f}});
    if (m_reportTable.title.empty())
    {
        rt().draw(makeText("Pick a report and press Run.", 15,
            {330.0f, 360.0f}, ui::theme::Muted));
        return;
    }
    rt().draw(makeText(m_reportTable.title, 15, {272.0f, 196.0f}, ui::theme::Heading));
    drawReportTable({{262.0f, 226.0f}, {696.0f + rx, 320.0f}});
    if (!m_reportTable.summary.empty())
        rt().draw(makeText(m_reportTable.summary, 12, {272.0f, 564.0f}, ui::theme::ActiveText));
    if (!m_reportMessage.empty())
        rt().draw(makeText(m_reportMessage, 11, {272.0f, 580.0f}, ui::theme::Muted));
    m_reportPrevBtn.draw(rt());
    m_reportNextBtn.draw(rt());
}

void BarcodeStudioGui::drawReportTable(const sf::FloatRect& card)
{
    if (m_reportTable.headers.empty()) return;
    float x = card.position.x + 10.0f;
    const float colWidth = (card.size.x - 20.0f)
        / static_cast<float>(m_reportTable.headers.size());
    for (const std::string& h : m_reportTable.headers)
    {
        rt().draw(makeText(h, 11, {x, card.position.y + 6.0f}, ui::theme::Muted));
        x += colWidth;
    }
    const int total = static_cast<int>(m_reportTable.rows.size());
    const int maxScroll = std::max(0, total - 8);
    const int first = std::min(m_reportScroll, maxScroll);
    const int last = std::min(total, first + 8);
    float y = card.position.y + 30.0f;
    for (int i = first; i < last; ++i)
    {
        const std::vector<std::string>& row = m_reportTable.rows[static_cast<std::size_t>(i)];
        ui::theme::drawRoundedBox(rt(), {card.position.x, y}, {card.size.x, 32.0f},
            8.0f, ui::theme::LightPanel);
        float cx = card.position.x + 10.0f;
        for (const std::string& cell : row)
        {
            rt().draw(makeText(truncFront(cell, 24), 12, {cx, y + 8.0f}, ui::theme::Text));
            cx += colWidth;
        }
        y += 36.0f;
    }
    if (total == 0)
        rt().draw(makeText("(empty report)", 13,
            {card.position.x + 10.0f, card.position.y + 60.0f}, ui::theme::Muted));
}

void BarcodeStudioGui::drawHeldOrders()
{
    const float rx = m_w - 1000.0f;
    rt().draw(makeText("Held Orders", 25, {kLeft, 26.0f}, ui::theme::Heading));
    m_heldBackBtn.draw(rt());

    ui::theme::drawCard(rt(), {{246.0f, 92.0f}, {728.0f + rx, 448.0f}});
    rt().draw(makeText("On-hold and in-progress orders", 15,
        {272.0f, 108.0f}, ui::theme::Heading));
    rt().draw(makeText("Order Id", 11, {272.0f, 138.0f}, ui::theme::Muted));
    rt().draw(makeText("Date", 11, {372.0f, 138.0f}, ui::theme::Muted));
    rt().draw(makeText("Customer", 11, {472.0f, 138.0f}, ui::theme::Muted));
    rt().draw(makeText("Status", 11, {676.0f, 138.0f}, ui::theme::Muted));
    rt().draw(makeText("Payable", 11, {786.0f, 138.0f}, ui::theme::Muted));
    if (m_heldCache.empty())
        rt().draw(makeText("No held orders right now.", 14, {300.0f, 300.0f}, ui::theme::Muted));
    const int total = static_cast<int>(m_heldCache.size());
    const int maxScroll = std::max(0, total - 8);
    const int first = std::min(m_heldScroll, maxScroll);
    const int last = std::min(total, first + 8);
    float y = 168.0f;
    for (int i = first; i < last; ++i)
    {
        const Order& o = m_heldCache[static_cast<std::size_t>(i)];
        const bool selected = (m_heldDrop.selected() == i);
        ui::theme::drawRoundedBox(rt(), {262.0f, y}, {696.0f + rx, 40.0f}, 8.0f,
            selected ? ui::theme::Active : ui::theme::LightPanel);
        rt().draw(makeText(o.id, 13, {272.0f, y + 10.0f}, ui::theme::Heading));
        rt().draw(makeText(o.date, 13, {372.0f, y + 10.0f}, ui::theme::Text));
        rt().draw(makeText(truncFront(o.customerName, 24), 13, {472.0f, y + 10.0f}, ui::theme::Text));
        rt().draw(makeText(o.status, 13, {676.0f, y + 10.0f},
            o.status == "on-hold" ? ui::theme::ActiveText : ui::theme::Muted));
        rt().draw(makeText(pkr(o.paymentAmount), 13, {786.0f, y + 10.0f}, ui::theme::Text));
        y += 44.0f;
    }
    m_heldResumeBtn.draw(rt());
    m_heldDeleteBtn.draw(rt());
    m_heldPrevBtn.draw(rt());
    m_heldNextBtn.draw(rt());
    if (!m_heldScreenMessage.empty())
        rt().draw(makeText(m_heldScreenMessage, 12, {272.0f, 526.0f},
            m_heldScreenMessage.find("Could") == std::string::npos
                ? ui::theme::Success : ui::theme::Error));
}

void BarcodeStudioGui::drawOrderEdit()
{
    const float rx = m_w - 1000.0f;
    rt().draw(makeText("Edit Paid Order", 25, {kLeft, 26.0f}, ui::theme::Heading));
    m_oeBackBtn.draw(rt());

    ui::theme::drawCard(rt(), {{246.0f, 92.0f}, {728.0f + rx, 232.0f}});
    rt().draw(makeText("Load Order", 15, {272.0f, 108.0f}, ui::theme::Heading));
    rt().draw(makeText("Order Id (8 digits)", 11, {272.0f, 122.0f}, ui::theme::Muted));
    rt().draw(makeText("Customer", 11, {272.0f, 180.0f}, ui::theme::Muted));
    rt().draw(makeText("Contact", 11, {524.0f, 180.0f}, ui::theme::Muted));
    m_oeOrderIdInput.draw(rt());
    m_oeCustomerInput.draw(rt());
    m_oeContactInput.draw(rt());
    m_oeLoadBtn.draw(rt());
    drawManagerPill({{272.0f, 248.0f}, {200.0f, 28.0f}},
        m_oeManagerApproved ? "Approved" : "Awaiting Approval",
        m_oeManagerApproved);
    m_oeApproveBtn.draw(rt());
    m_oeSaveBtn.draw(rt());
    if (!m_oeMessage.empty())
        rt().draw(makeText(m_oeMessage, 12, {272.0f, 302.0f},
            m_oeMessage.find("Error") == std::string::npos
                && m_oeMessage.find("Cannot") == std::string::npos
                ? ui::theme::Success : ui::theme::Error));

    ui::theme::drawCard(rt(), {{246.0f, 336.0f}, {728.0f + rx, 288.0f}});
    rt().draw(makeText("Order Lines", 15, {272.0f, 352.0f}, ui::theme::Heading));
    rt().draw(makeText("Acronym", 11, {272.0f, 380.0f}, ui::theme::Muted));
    rt().draw(makeText("Name", 11, {366.0f, 380.0f}, ui::theme::Muted));
    rt().draw(makeText("Qty", 11, {676.0f, 380.0f}, ui::theme::Muted));
    float y = 404.0f;
    if (m_oeLines.empty())
        rt().draw(makeText("Load an order to see its lines.", 13, {272.0f, 420.0f}, ui::theme::Muted));
    for (const auto& p : m_oeLines)
    {
        ui::theme::drawRoundedBox(rt(), {262.0f, y}, {696.0f + rx, 40.0f}, 8.0f, ui::theme::LightPanel);
        rt().draw(makeText(p.first.acronym, 13, {272.0f, y + 10.0f}, ui::theme::Heading));
        rt().draw(makeText(truncFront(p.first.name, 30), 13, {366.0f, y + 10.0f}, ui::theme::Text));
        rt().draw(makeText("-", 15, {644.0f, y + 9.0f}, ui::theme::Heading));
        rt().draw(makeText(std::to_string(p.second), 13, {676.0f, y + 10.0f}, ui::theme::Text));
        rt().draw(makeText("+", 15, {714.0f, y + 9.0f}, ui::theme::Heading));
        y += 44.0f;
        if (y > 560.0f) break;
    }
}

void BarcodeStudioGui::drawBillPrint()
{
    const float rx = m_w - 1000.0f;
    rt().draw(makeText("Bill", 25, {kLeft, 26.0f}, ui::theme::Heading));
    m_printCloseBtn.draw(rt());
    const Bill& b = m_lastBill.bill;
    if (b.id.empty())
    {
        ui::theme::drawCard(rt(), {{246.0f, 92.0f}, {728.0f + rx, 484.0f}});
        rt().draw(makeText("No bill to show yet. Generate one from the POS screen.",
            15, {300.0f, 300.0f}, ui::theme::Muted));
        return;
    }
    ui::theme::drawCard(rt(), {{246.0f, 92.0f}, {728.0f + rx, 448.0f}});
    rt().draw(makeText("Bill " + b.id + "  -  Order " + b.orderId, 15,
        {272.0f, 108.0f}, ui::theme::Heading));
    rt().draw(makeText("Customer: " + b.customerName, 12, {272.0f, 132.0f}, ui::theme::Text));
    rt().draw(makeText("Date: " + b.billDate + "  Time: " + b.billTime,
        12, {272.0f, 150.0f}, ui::theme::Text));
    rt().draw(makeText("Product", 11, {272.0f, 180.0f}, ui::theme::Muted));
    rt().draw(makeText("Qty", 11, {446.0f, 180.0f}, ui::theme::Muted));
    rt().draw(makeText("Unit Price", 11, {506.0f, 180.0f}, ui::theme::Muted));
    rt().draw(makeText("Line Disc", 11, {616.0f, 180.0f}, ui::theme::Muted));
    float y = 204.0f;
    for (const BillDetail& d : m_lastBill.billDetail)
    {
        ui::theme::drawRoundedBox(rt(), {262.0f, y}, {696.0f + rx, 28.0f}, 6.0f, ui::theme::LightPanel);
        rt().draw(makeText(d.productAcronym, 12, {272.0f, y + 6.0f}, ui::theme::Heading));
        rt().draw(makeText(std::to_string(d.quantity), 12, {446.0f, y + 6.0f}, ui::theme::Text));
        rt().draw(makeText(stringutils::money(d.unitPrice), 12, {506.0f, y + 6.0f}, ui::theme::Text));
        rt().draw(makeText(stringutils::money(d.lineDiscount), 12, {616.0f, y + 6.0f}, ui::theme::Text));
        y += 30.0f;
        if (y > 300.0f) break;
    }
    rt().draw(makeText("Subtotal: " + pkr(b.totalWithoutTax), 12, {272.0f, 316.0f}, ui::theme::Text));
    rt().draw(makeText("Tax: " + pkr(b.totalTax), 12, {272.0f, 334.0f}, ui::theme::Text));
    rt().draw(makeText("Discount: -" + pkr(b.totalDiscount), 12, {272.0f, 352.0f}, ui::theme::Text));
    rt().draw(makeText("Seasonal: -" + pkr(b.seasonalDiscount), 12, {272.0f, 370.0f}, ui::theme::Text));
    rt().draw(makeText("Special: -" + pkr(b.specialDiscount), 12, {272.0f, 388.0f}, ui::theme::Text));
    rt().draw(makeText("Payable: " + pkr(b.totalPayable), 15, {272.0f, 412.0f}, ui::theme::ActiveText));
    rt().draw(makeText("Received: " + pkr(b.amountReceived), 12, {500.0f, 316.0f}, ui::theme::Text));
    rt().draw(makeText("Balance: " + pkr(b.balanceReturned), 12, {500.0f, 334.0f}, ui::theme::Text));
    rt().draw(makeText("Status: " + b.paymentStatus, 12, {500.0f, 352.0f}, ui::theme::Text));
    m_printPdfBtn.draw(rt());
    if (!m_printMessage.empty())
        rt().draw(makeText(m_printMessage, 12, {272.0f, 616.0f},
            m_printMessage.find("saved") != std::string::npos
                ? ui::theme::Success : ui::theme::Error));
}

// ========================================================== PHASE 3 ACTIONS
// Every one of these only reads from widgets, calls a service, writes a
// status string, and refreshes the cache that the matching draw* method
// reads. No rule and no file IO lives here (SRP).

void BarcodeStudioGui::refreshCategories()
{
    m_categoriesCache = m_categoryService.listAll();
    std::vector<std::string> ids;
    for (const Category& c : m_categoriesCache)
        ids.push_back(c.id + " " + c.acronym);
    m_peCategoryDrop.setOptions(ids);
    if (m_peCategoryDrop.selected() < 0 && !ids.empty())
        m_peCategoryDrop.setSelected(0);
}

void BarcodeStudioGui::categoryAdd()
{
    const std::string id = trimStr(m_catIdInput.getText());
    const std::string name = trimStr(m_catNameInput.getText());
    const std::string acronym = trimStr(m_catAcronymInput.getText());
    const std::string err = m_categoryService.addCategory(id, name, acronym);
    if (err == "bad-id") { m_catMessage = "Id must be exactly 2 digits."; return; }
    if (err == "duplicate") { m_catMessage = "That category id is already used."; return; }
    if (err == "bad-name") { m_catMessage = "Category name is required."; return; }
    if (err == "bad-acronym") { m_catMessage = "Acronym is required."; return; }
    if (err == "write-failed") { m_catMessage = "Could not save PCategory.txt."; return; }
    m_catMessage = "Category added.";
    m_catIdInput.setText("");
    m_catNameInput.setText("");
    m_catAcronymInput.setText("");
    refreshCategories();
    celebrate("Category added!", {610.0f, 220.0f});
}

void BarcodeStudioGui::refreshSeasons()
{
    m_seasonsCache = m_seasonsService.listAll();
    if (m_seasonSelectedIdx >= static_cast<int>(m_seasonsCache.size()))
        m_seasonSelectedIdx = m_seasonsCache.empty() ? -1
            : static_cast<int>(m_seasonsCache.size()) - 1;
}

void BarcodeStudioGui::seasonAdd()
{
    double pct = 0.0;
    if (!parseDouble(trimStr(m_seasonPctInput.getText()), pct))
    { m_seasonMessage = "Percent must be a number 0-100."; return; }
    const std::string err = m_seasonsService.addSeason(
        trimStr(m_seasonStartInput.getText()),
        trimStr(m_seasonEndInput.getText()), pct, true);
    if (err == "bad-date") { m_seasonMessage = "Dates must be YYYY-MM-DD."; return; }
    if (err == "bad-range") { m_seasonMessage = "Start must be on or before end."; return; }
    if (err == "bad-percent") { m_seasonMessage = "Percent must be 0-100."; return; }
    if (err == "write-failed") { m_seasonMessage = "Could not save Seasons.txt."; return; }
    m_seasonMessage = "Season added (active).";
    m_seasonStartInput.setText("");
    m_seasonEndInput.setText("");
    m_seasonPctInput.setText("");
    refreshSeasons();
}

void BarcodeStudioGui::seasonToggle()
{
    if (m_seasonSelectedIdx < 0
        || m_seasonSelectedIdx >= static_cast<int>(m_seasonsCache.size()))
    { m_seasonMessage = "Select a season row first."; return; }
    const Season& s = m_seasonsCache[static_cast<std::size_t>(m_seasonSelectedIdx)];
    const std::string err = m_seasonsService.setActive(s.startDate, !s.isActive);
    if (err == "not-found") { m_seasonMessage = "That season is gone - refresh."; return; }
    if (err == "write-failed") { m_seasonMessage = "Could not save Seasons.txt."; return; }
    m_seasonMessage = "Season toggled.";
    refreshSeasons();
}

void BarcodeStudioGui::refreshInventory()
{
    m_productsCache = m_productService.listAll();
    const int maxScroll = std::max(0, static_cast<int>(m_productsCache.size()) - 8);
    if (m_productsScroll > maxScroll) m_productsScroll = maxScroll;
    if (m_invSelectedIdx >= static_cast<int>(m_productsCache.size()))
        m_invSelectedIdx = -1;
}

void BarcodeStudioGui::inventorySet()
{
    if (m_invSelectedIdx < 0
        || m_invSelectedIdx >= static_cast<int>(m_productsCache.size()))
    { m_invMessage = "Click a product first."; return; }
    int qty = 0;
    if (!parseIntStr(trimStr(m_invQtyInput.getText()), qty) || qty < 0)
    { m_invMessage = "Stock must be a non-negative number."; return; }
    const Product& p = m_productsCache[static_cast<std::size_t>(m_invSelectedIdx)];
    if (!m_productService.setStock(p.id, qty))
    { m_invMessage = "Could not update product.txt."; return; }
    m_invMessage = "Stock updated.";
    refreshInventory();
}

void BarcodeStudioGui::productEditLoad()
{
    int id = 0;
    if (!parseIntStr(trimStr(m_peIdInput.getText()), id) || id <= 0)
    { m_peMessage = "Enter a positive product id first."; return; }
    for (const Product& p : m_productsCache)
    {
        if (p.id != id) continue;
        m_peIdInput.setText(std::to_string(p.id));
        m_peNameInput.setText(p.name);
        m_peAcronymInput.setText(p.acronym);
        m_pePriceInput.setText(stringutils::money(p.unitPrice));
        m_peTaxInput.setText(stringutils::money(p.taxPerUnit));
        m_peDiscountInput.setText(stringutils::money(p.discountPerUnit));
        m_peQtyInput.setText(std::to_string(p.availableQuantity));
        m_peUnitDrop.setOptions({"count", "weight"});
        m_peUnitDrop.setSelected(p.unit == "weight" ? 1 : 0);
        for (std::size_t i = 0; i < m_categoriesCache.size(); ++i)
            if (m_categoriesCache[i].id == p.categoryId)
            { m_peCategoryDrop.setSelected(static_cast<int>(i)); break; }
        m_peGeneratedBarcode = p.barcode;
        m_peMessage = "Loaded product " + std::to_string(p.id) + ".";
        return;
    }
    m_peMessage = "No product with that id.";
}

void BarcodeStudioGui::productEditSave()
{
    int id = 0;
    if (!parseIntStr(trimStr(m_peIdInput.getText()), id) || id <= 0)
    { m_peMessage = "Enter a positive product id."; return; }
    const std::string name = trimStr(m_peNameInput.getText());
    const std::string acronym = trimStr(m_peAcronymInput.getText());
    double price = 0.0, tax = 0.0, disc = 0.0;
    if (!parseDouble(trimStr(m_pePriceInput.getText()), price)
        || !parseDouble(trimStr(m_peTaxInput.getText()), tax)
        || !parseDouble(trimStr(m_peDiscountInput.getText()), disc))
    { m_peMessage = "Price, tax and discount must be numbers."; return; }
    int qty = 0;
    if (!parseIntStr(trimStr(m_peQtyInput.getText()), qty) || qty < 0)
    { m_peMessage = "Stock must be a non-negative integer."; return; }
    const std::string catText = m_peCategoryDrop.selectedText();
    if (catText.size() < 2) { m_peMessage = "Pick a category."; return; }
    const std::string categoryId = catText.substr(0, 2);

    Product p;
    p.id = id;
    p.name = name;
    p.acronym = acronym;
    p.unit = m_peUnitDrop.selectedText();
    p.unitPrice = price;
    p.taxPerUnit = tax;
    p.discountPerUnit = disc;
    p.availableQuantity = qty;
    p.categoryId = categoryId;
    p.barcode = ProductService::generateBarcode(categoryId, id);
    p.barcodeFile = "tests/barcode_" + p.barcode + ".bmp";

    bool exists = false;
    for (const Product& existing : m_productsCache)
        if (existing.id == id) { exists = true; break; }

    const std::string err = exists
        ? m_productService.updateProduct(p)
        : m_productService.addProduct(p);
    if (err == "bad-category") { m_peMessage = "That category id does not exist."; return; }
    if (err == "bad-id") { m_peMessage = "Id is not valid."; return; }
    if (err == "bad-barcode") { m_peMessage = "That barcode is already used."; return; }
    if (err == "bad-fields") { m_peMessage = "Name and acronym are required."; return; }
    if (err == "write-failed") { m_peMessage = "Could not save product.txt."; return; }
    m_peMessage = exists ? "Product saved." : "Product added.";
    refreshInventory();
    refreshProducts();
    m_peGeneratedBarcode = p.barcode;
    celebrate(exists ? "Product saved!" : "Product added!", {610.0f, 320.0f});
}

void BarcodeStudioGui::productEditGenerateBarcode()
{
    int id = 0;
    if (!parseIntStr(trimStr(m_peIdInput.getText()), id) || id <= 0)
    { m_peMessage = "Enter a product id first."; return; }
    const std::string catText = m_peCategoryDrop.selectedText();
    if (catText.size() < 2) { m_peMessage = "Pick a category."; return; }
    m_peGeneratedBarcode = ProductService::generateBarcode(catText.substr(0, 2), id);
    m_peMessage = "Barcode: " + m_peGeneratedBarcode +
        " (saved automatically when you press Save).";
}

void BarcodeStudioGui::runReport()
{
    const std::string sel = m_reportDrop.selectedText();
    if (sel == "incomplete") m_reportTable = m_reportService.incompleteOrders();
    else if (sel == "completed") m_reportTable = m_reportService.completedOrders();
    else if (sel == "daily" || sel == "weekly" || sel == "monthly"
        || sel == "yearly" || sel == "custom")
        m_reportTable = m_reportService.cashSales(sel,
            trimStr(m_reportFromInput.getText()),
            trimStr(m_reportToInput.getText()));
    else if (sel == "sales_discounts") m_reportTable = m_reportService.allSalesWithDiscounts();
    else if (sel == "top_sold") m_reportTable = m_reportService.topTenSoldItems();
    else if (sel == "top_stock") m_reportTable = m_reportService.topTenRemainingStock();
    else { m_reportMessage = "Pick a report first."; return; }
    m_reportScroll = 0;
    m_reportMessage = std::to_string(m_reportTable.rows.size()) + " row(s).";
}

void BarcodeStudioGui::refreshHeldOrders()
{
    m_heldCache.clear();
    m_heldIds.clear();
    for (const Order& o : m_orderRepo.loadAll())
    {
        if (o.status != "on-hold" && o.status != "in-progress") continue;
        m_heldCache.push_back(o);
        m_heldIds.push_back(o.id);
    }
    m_heldDrop.setOptions(m_heldIds);
    if (m_heldDrop.selected() < 0 && !m_heldIds.empty())
        m_heldDrop.setSelected(0);
    if (m_heldScroll > std::max(0, static_cast<int>(m_heldCache.size()) - 8))
        m_heldScroll = std::max(0, static_cast<int>(m_heldCache.size()) - 8);
}

void BarcodeStudioGui::heldResumeSelected()
{
    const int idx = m_heldDrop.selected();
    if (idx < 0 || idx >= static_cast<int>(m_heldCache.size()))
    { m_heldScreenMessage = "No held order selected."; return; }
    const std::string orderId = m_heldCache[static_cast<std::size_t>(idx)].id;
    m_orderService.clear();
    const std::string err = m_orderService.resumeOrder(orderId);
    if (err == "not-found") { m_heldScreenMessage = "Could not find that order."; return; }
    if (err == "wrong-status")
    { m_heldScreenMessage = "That order is not on-hold any more."; return; }
    m_heldScreenMessage = "Resumed order " + orderId + ".";
    switchView(View::PosOrder);
    m_posBarcode.setFocused(true);
}

void BarcodeStudioGui::heldDeleteSelected()
{
    const int idx = m_heldDrop.selected();
    if (idx < 0 || idx >= static_cast<int>(m_heldCache.size()))
    { m_heldScreenMessage = "No held order selected."; return; }
    const Order& o = m_heldCache[static_cast<std::size_t>(idx)];
    Order updated = o;
    updated.status = "archived";
    std::vector<Order> all = m_orderRepo.loadAll();
    for (Order& existing : all)
        if (existing.id == updated.id) existing = updated;
    if (!RecordFile::writeAllLines(m_orderRepo.filePath(),
        [&]() {
            std::vector<std::string> lines;
            for (const Order& x : all) lines.push_back(OrderSerialiser::format(x));
            return lines;
        }()))
    { m_heldScreenMessage = "Could not archive the order."; return; }
    m_heldScreenMessage = "Archived order " + updated.id + ".";
    refreshHeldOrders();
}

void BarcodeStudioGui::orderEditLoad()
{
    const std::string id = trimStr(m_oeOrderIdInput.getText());
    if (id.empty()) { m_oeMessage = "Enter the order id first."; return; }
    CompletedBill out;
    const std::string gate = m_orderService.editPaidOrder(id,
        m_oeManagerApproved, out);
    if (gate == "not-approved") { m_oeMessage = "Manager approval is required."; return; }
    if (gate == "not-found") { m_oeMessage = "No such order."; return; }
    if (gate == "wrong-status") { m_oeMessage = "Only paid orders can be edited."; return; }
    if (gate == "too-old") { m_oeMessage = "More than 7 days have passed."; return; }
    m_oeCustomerInput.setText(out.order.customerName);
    m_oeContactInput.setText(out.order.customerContact);
    m_oeLines.clear();
    for (const OrderDetail& d : m_detailRepo.loadForOrder(id))
    {
        const auto p = m_productService.findByBarcode(d.productBarcode);
        if (p) m_oeLines.push_back({*p, d.quantity});
    }
    m_oeMessage = "Loaded order " + id + ".";
}

void BarcodeStudioGui::orderEditApprove()
{
    if (m_role != UserRole::Manager)
    { m_oeMessage = "Only a manager can approve."; return; }
    m_oeManagerApproved = true;
    m_oeMessage = "Manager approved. You may now Save.";
}

void BarcodeStudioGui::orderEditSave()
{
    if (!m_oeManagerApproved) { m_oeMessage = "Manager approval is required first."; return; }
    const std::string id = trimStr(m_oeOrderIdInput.getText());
    if (id.empty() || m_oeLines.empty()) { m_oeMessage = "Load an order first."; return; }
    std::vector<CartLine> lines;
    for (const auto& row : m_oeLines) lines.push_back({row.first, row.second});
    CompletedBill out;
    const std::string err = m_orderService.saveEditedPaidOrder(id,
        trimStr(m_oeCustomerInput.getText()), trimStr(m_oeContactInput.getText()),
        lines, m_oeManagerApproved, out);
    if (err == "not-approved") { m_oeMessage = "Manager approval is required first."; return; }
    if (err == "not-found") { m_oeMessage = "No such order."; return; }
    if (err == "wrong-status") { m_oeMessage = "Only paid orders can be edited."; return; }
    if (err == "too-old") { m_oeMessage = "More than 7 days have passed."; return; }
    if (err == "no-customer") { m_oeMessage = "Customer name is required."; return; }
    if (err == "low-stock") { m_oeMessage = "Not enough stock for one of the lines."; return; }
    if (err == "write-failed") { m_oeMessage = "Could not save the changes."; return; }
    m_lastBill = out;
    m_showBill = true;
    refreshProducts();
    m_oeMessage = "Order " + id + " updated. Bill " + out.bill.id + " re-issued.";
}

void BarcodeStudioGui::posHold()
{
    if (m_orderService.empty()) { m_posMessage = "Nothing to hold."; m_posMessageOk = false; return; }
    if (trimStr(m_posCustomer.getText()).empty())
    { m_posMessage = "Enter the customer name first."; m_posMessageOk = false; return; }
    std::string merchant = m_merchantDrop.selectedText();
    if (merchant == "-") merchant.clear();
    const std::string err = m_orderService.holdOrder(
        trimStr(m_posCustomer.getText()), trimStr(m_posContact.getText()),
        kPayModes[static_cast<std::size_t>(m_modeDrop.selected())], merchant);
    if (err == "empty-order") { m_posMessage = "Nothing to hold."; m_posMessageOk = false; return; }
    if (err == "no-customer") { m_posMessage = "Enter the customer name first."; m_posMessageOk = false; return; }
    if (err == "write-failed") { m_posMessage = "Could not save the held order."; m_posMessageOk = false; return; }
    m_posMessage = "Order held.";
    m_posMessageOk = true;
    posNewOrder();
    refreshHeldOrders();
}

void BarcodeStudioGui::posResume()
{
    const int idx = m_heldDrop.selected();
    if (idx < 0 || idx >= static_cast<int>(m_heldIds.size()))
    { m_posMessage = "No held order selected."; m_posMessageOk = false; return; }
    m_orderService.clear();
    const std::string err = m_orderService.resumeOrder(m_heldIds[static_cast<std::size_t>(idx)]);
    if (err == "not-found") { m_posMessage = "Could not load that held order."; m_posMessageOk = false; return; }
    if (err == "wrong-status") { m_posMessage = "That order is no longer on-hold."; m_posMessageOk = false; return; }
    m_posMessage = "Order resumed.";
    m_posMessageOk = true;
    refreshHeldOrders();
}

void BarcodeStudioGui::posPrintPdf()
{
    if (m_lastBill.bill.id.empty())
    { m_printMessage = "No bill generated yet."; return; }
    const std::string path = pdfbill::writePdf("bills", m_lastBill.bill,
        m_lastBill.billDetail);
    if (path.empty()) { m_printMessage = "Could not write the PDF."; return; }
    m_printMessage = "PDF saved: " + path;
}

void BarcodeStudioGui::resetScan()
{
    m_decoded.clear();
    m_hasGeneratedBarcode = false;
    m_generatedBarcodePath.clear();
    resetScanState();
    showToast("Scanning again - point the webcam at a barcode.", true);
}

// =============================================================== PHASE 2 POS
// The screens below contain NO file or money logic: they call
// ProductService / OrderService, which own those rules (SRP). The GUI
// only collects input, shows results, and celebrates a little.
void BarcodeStudioGui::refreshProducts()
{
    m_productRepo = ProductFileRepository();
    m_orderRepo = OrderFileRepository();
    m_detailRepo = OrderDetailFileRepository();
    m_billRepo = BillFileRepository();
    m_billDetailRepo = BillDetailFileRepository();
    m_categoryRepo = CategoryFileRepository();
    m_seasonsRepo = SeasonsFileRepository();
    m_productsCache = m_productService.listAll();
    const int maxScroll = std::max(0, static_cast<int>(m_productsCache.size()) - 8);
    m_productsScroll = std::min(m_productsScroll, maxScroll);
}

void BarcodeStudioGui::posDescribe()
{
    const std::string barcode = trimmed(m_posBarcode.getText());
    if (barcode.empty())
    {
        m_posMessage = "Type or scan a barcode first, then press Description.";
        m_posMessageOk = false;
        return;
    }
    const auto product = m_productService.findByBarcode(barcode);
    m_lastScannedCode = barcode;
    if (!product)
    {
        m_descProduct.reset();
        m_posMessage = "No product with barcode " + barcode + " in Data/product.txt.";
        m_posMessageOk = false;
        showToast("Product not found in Data/product.txt.", false);
        return;
    }
    m_descProduct = product;
    m_posMessage = "Showing the description of " + product->name + ".";
    m_posMessageOk = true;
}

void BarcodeStudioGui::posAdd()
{
    const std::string barcode = trimmed(m_posBarcode.getText());
    const int qty = parseQty(trimmed(m_posQty.getText()));
    const std::string err = m_orderService.addLine(barcode, qty);
    if (err == "not-found")
    {
        m_posMessage = "No product with barcode " + barcode + " in Data/product.txt.";
        m_posMessageOk = false;
        showToast("Product not found.", false);
        return;
    }
    if (err == "low-stock")
    {
        const auto p = m_productService.findByBarcode(barcode);
        m_posMessage = "Not enough stock" + (p ? " for " + p->name + " (available " +
            std::to_string(p->availableQuantity) + ")." : ".");
        m_posMessageOk = false;
        showToast("Not enough stock available.", false);
        return;
    }
    if (err == "bad-quantity")
    {
        m_posMessage = "Quantity must be at least 1.";
        m_posMessageOk = false;
        return;
    }
    const auto p = m_productService.findByBarcode(barcode);
    if (p) m_descProduct = p;
    m_posMessage = "Added " + (p ? p->name : barcode) + " x" + std::to_string(qty) + " to the order.";
    m_posMessageOk = true;
    spawnBurst({640.0f, 320.0f}, 12);
    m_posQty.setText("1");
    m_posBarcode.setFocused(true);
}

bool BarcodeStudioGui::posAddOne(const std::string& barcode)
{
    // One-unit add for the scan flows (POS camera scan, and the Add
    // to Order buttons on the Camera/File screens): the scan IS the
    // add, so there is no qty typing and no separate Add press.
    // Stock is still enforced by OrderService, exactly like posAdd().
    const std::string err = m_orderService.addLine(barcode, 1);
    if (err == "not-found")
    {
        m_posMessage = "No product with barcode " + barcode + " in Data/product.txt.";
        m_posMessageOk = false;
        return false;
    }
    if (err == "low-stock")
    {
        const auto p = m_productService.findByBarcode(barcode);
        m_posMessage = "Not enough stock" + (p ? " for " + p->name + " (available " +
            std::to_string(p->availableQuantity) + ")." : ".");
        m_posMessageOk = false;
        return false;
    }
    if (!err.empty())
    {
        m_posMessage = "Could not add that item to the order.";
        m_posMessageOk = false;
        return false;
    }
    const auto p = m_productService.findByBarcode(barcode);
    if (p) m_descProduct = p;
    int inCart = 0;
    for (const auto& line : m_orderService.lines())
        if (line.product.barcode == barcode) inCart = line.quantity;
    m_posMessage = "Added " + (p ? p->name : barcode) + " to the order" +
        (inCart > 1 ? " (x" + std::to_string(inCart) + " in cart)." : ".");
    m_posMessageOk = true;
    spawnBurst({640.0f, 320.0f}, 12);
    return true;
}

void BarcodeStudioGui::addDecodedToOrder()
{
    // The Camera/File screens decoded a product: one click puts it
    // in the order and lands on the POS screen - no second scan.
    if (m_decoded.empty() || !m_descProduct) return;
    if (!posAddOne(m_decoded))
    {
        m_mascotText = "Couldn't add that -\nnot enough stock!";
        m_mascotHappy = 1.5f;
        return;
    }
    switchView(View::PosOrder);
    m_posBarcode.setText(m_decoded);
}

void BarcodeStudioGui::posCheckout()
{
    const std::string contact = trimmed(m_posContact.getText());
    if (!contact.empty() && !stringutils::isValidPhone(contact))
    {
        m_posMessage = "Contact number must be exactly 11 digits.";
        m_posMessageOk = false;
        m_posContact.setFocused(true);
        showToast("Contact number must be exactly 11 digits.", false);
        return;
    }
    double specialPct = 0.0;
    const std::string specialText = trimStr(m_posSpecialPct.getText());
    if (!specialText.empty() && !parseDouble(specialText, specialPct))
    {
        m_posMessage = "Special discount must be a number 0-8.";
        m_posMessageOk = false;
        m_posSpecialPct.setFocused(true);
        return;
    }
    if (specialPct < 0.0) specialPct = 0.0;
    if (specialPct > 8.0)
    {
        specialPct = 8.0;
        m_posSpecialPct.setText("8");
        m_posMessage = "Special discount clamped to 8%.";
    }
    double amountReceived = 0.0;
    const std::string amountText = trimStr(m_posAmount.getText());
    if (!amountText.empty() && !parseDouble(amountText, amountReceived))
    {
        m_posMessage = "Amount received must be a number.";
        m_posMessageOk = false;
        m_posAmount.setFocused(true);
        return;
    }

    CompletedBill bill;
    std::string merchant = m_merchantDrop.selectedText();
    if (merchant == "-") merchant.clear();
    const std::string err = m_orderService.checkout(
        trimmed(m_posCustomer.getText()), contact,
        kStatuses[static_cast<std::size_t>(m_statusDrop.selected())],
        kPayModes[static_cast<std::size_t>(m_modeDrop.selected())],
        merchant, specialPct, amountReceived, bill);
    if (err == "no-customer")
    {
        m_posMessage = "Enter the customer name before generating the bill.";
        m_posMessageOk = false;
        m_posCustomer.setFocused(true);
        showToast("Customer name is required for the bill.", false);
        return;
    }
    if (err == "empty-order")
    {
        m_posMessage = "Add at least one product before generating the bill.";
        m_posMessageOk = false;
        return;
    }
    if (err == "low-stock")
    {
        refreshProducts();
        m_posMessage = "Stock changed - some item no longer has enough quantity.";
        m_posMessageOk = false;
        showToast("Stock no longer sufficient for this order.", false);
        return;
    }
    if (err == "bad-discount")
    {
        m_posMessage = "Special discount must be between 0 and 8 percent.";
        m_posMessageOk = false;
        return;
    }
    if (!err.empty())
    {
        m_posMessage = "Could not save the bill (" + err + ").";
        m_posMessageOk = false;
        return;
    }
    m_lastBill = bill;
    m_showBill = true;
    refreshProducts();
    refreshHeldOrders();
    m_posMessage = "Bill " + bill.bill.id + " generated and saved.";
    m_posMessageOk = true;
    celebrate("Bill generated!\nOrder saved!", {610.0f, 330.0f});
    showToast("Bill " + bill.bill.id + " saved to Data/bill.txt.", true);
}

void BarcodeStudioGui::posNewOrder()
{
    m_orderService.clear();
    m_showBill = false;
    m_printMessage.clear();
    m_posMessage = "New order started. Scan the first product!";
    m_posMessageOk = true;
    m_posBarcode.setText("");
    m_posQty.setText("1");
    m_posSpecialPct.setText("0");
    m_posAmount.setText("");
    m_posBarcode.setFocused(true);
    resetScanState();
    refreshHeldOrders();
    if (m_cameraRunning) { stopCamera(); startCamera(); }
}

void BarcodeStudioGui::drawPosCameraCard(const sf::FloatRect& card)
{
    ui::theme::drawCard(rt(), card);
    const float x = card.position.x + 20.0f;
    rt().draw(makeText("Live Camera", 15, {x, card.position.y + 14.0f}, ui::theme::Heading));
    ui::theme::drawStatus(rt(), m_font, {card.position.x + card.size.x - 76.0f, card.position.y + 12.0f},
        {56.0f, 22.0f}, "LIVE", true);
    const sf::FloatRect video({card.position.x + 12.0f, card.position.y + 42.0f},
        {card.size.x - 24.0f, 96.0f});
    sf::RectangleShape videoBg(video.size);
    videoBg.setPosition(video.position);
    videoBg.setFillColor(sf::Color(24, 16, 22));
    rt().draw(videoBg);
    if (m_hasCameraFrame)
    {
        sf::Sprite camSprite(m_cameraTexture);
        const sf::Vector2u s = m_cameraTexture.getSize();
        if (s.x > 0 && s.y > 0)
        {
            const float scale = std::min(video.size.x / s.x, video.size.y / s.y);
            camSprite.setScale({scale, scale});
            camSprite.setPosition({video.position.x + (video.size.x - s.x * scale) / 2.0f,
                video.position.y + (video.size.y - s.y * scale) / 2.0f});
            rt().draw(camSprite);
        }
    }
    else
    {
        rt().draw(makeText("Connecting...", 12, {video.position.x + 62.0f, video.position.y + 40.0f},
            ui::theme::Muted));
    }
    if (m_descProduct)
        rt().draw(makeText("Last scanned: " + truncFront(m_descProduct->name, 18) + " - " +
            pkr(m_descProduct->unitPrice), 11, {x, card.position.y + 148.0f}, ui::theme::ActiveText));
    else if (!m_lastScannedCode.empty())
        rt().draw(makeText("Scanned " + m_lastScannedCode + " - no product found",
            11, {x, card.position.y + 148.0f}, ui::theme::Error));
    else
        rt().draw(makeText("Point a barcode at the camera.", 11,
            {x, card.position.y + 148.0f}, ui::theme::Muted));
}

void BarcodeStudioGui::drawDescriptionCard(const sf::FloatRect& card)
{
    ui::theme::drawCard(rt(), card);
    const float x = card.position.x + 20.0f;
    rt().draw(makeText("Product Description", 15, {x, card.position.y + 16.0f}, ui::theme::Heading));
    if (!m_descProduct)
    {
        rt().draw(makeText("No product selected yet.\nScan a barcode with the\ncamera, type one and press\nDescription, or add an item.",
            12, {x, card.position.y + 52.0f}, ui::theme::Muted));
        return;
    }
    const Product& p = *m_descProduct;
    rt().draw(makeText(truncFront(p.name, 24), 16, {x, card.position.y + 46.0f}, ui::theme::ActiveText));
    rt().draw(makeText("ID " + std::to_string(p.id) + "  -  " + p.acronym + "  -  " + p.barcode,
        11, {x, card.position.y + 74.0f}, ui::theme::Muted));
    rt().draw(makeText("Price: " + pkr(p.unitPrice) + " / " + p.unit, 12,
        {x, card.position.y + 98.0f}, ui::theme::Text));
    rt().draw(makeText("Tax " + pkr(p.taxPerUnit) + "  -  Discount " + pkr(p.discountPerUnit),
        12, {x, card.position.y + 120.0f}, ui::theme::Text));
    rt().draw(makeText("Stock available: " + std::to_string(p.availableQuantity), 12,
        {x, card.position.y + 142.0f},
        p.availableQuantity > 0 ? ui::theme::Success : ui::theme::Error));
}

void BarcodeStudioGui::drawPosOrder()
{
    rt().draw(makeText("POS - Record Order", 25, {kLeft, 26.0f}, ui::theme::Heading));

    const float rx = m_w - 1000.0f;
    // Right column is wider than before (284) and starts further
    // left, so the description / customer sections are not cramped
    // against the window's right edge.
    const float cx = 690.0f + rx;  // right column x (anchored to the window's right edge)
    // --- entry card
    ui::theme::drawCard(rt(), {{246.0f, 92.0f}, {432.0f + rx, 178.0f}});
    rt().draw(makeText("Scan / Enter Barcode", 15, {272.0f, 108.0f}, ui::theme::Heading));
    rt().draw(makeText("Qty", 12, {502.0f, 114.0f}, ui::theme::Muted));
    m_posBarcode.setSize({218.0f, 42.0f});
    m_posQty.setPosition({502.0f, 136.0f});
    rt().draw(makeText(m_cameraRunning ? "Camera: LIVE" : "Camera is off",
        11, {570.0f + rx, 112.0f}, m_cameraRunning ? ui::theme::Success : ui::theme::Muted));
    m_posCamBtn.setPosition({570.0f + rx, 136.0f});
    m_posBarcode.draw(rt());
    m_posQty.draw(rt());
    m_posCamBtn.draw(rt());
    m_posDescBtn.draw(rt());
    m_posAddBtn.draw(rt());
    rt().draw(makeText(shorten(m_posMessage, 74), 12, {272.0f, 246.0f},
        m_posMessageOk ? ui::theme::Success : ui::theme::Error));

    // --- description card (requirement: Description button / scan to view)
    if (m_cameraRunning) drawPosCameraCard({{cx, 92.0f}, {284.0f, 178.0f}});
    else drawDescriptionCard({{cx, 92.0f}, {284.0f, 178.0f}});

    // --- cart card
    ui::theme::drawCard(rt(), {{246.0f, 282.0f}, {432.0f + rx, 342.0f}});
    rt().draw(makeText("Order Items", 15, {272.0f, 300.0f}, ui::theme::Heading));
    rt().draw(makeText("Product", 11, {272.0f, 330.0f}, ui::theme::Muted));
    rt().draw(makeText("Qty", 11, {545.0f, 330.0f}, ui::theme::Muted));
    rt().draw(makeText("Line Total", 11, {582.0f + rx, 330.0f}, ui::theme::Muted));
    const auto& lines = m_orderService.lines();
    if (lines.empty())
        rt().draw(makeText("The order is empty - scan or add the first product.", 13,
            {290.0f, 420.0f}, ui::theme::Muted));
    const std::size_t shown = std::min<std::size_t>(lines.size(), 4);
    for (std::size_t i = 0; i < shown; ++i)
    {
        const CartLine& line = lines[i];
        const float ry = 352.0f + static_cast<float>(i) * 48.0f;
        ui::theme::drawRoundedBox(rt(), {262.0f, ry + 4.0f}, {400.0f + rx, 40.0f}, 10.0f, ui::theme::LightPanel);
        rt().draw(makeText(line.product.acronym, 13, {272.0f, ry + 15.0f}, ui::theme::Heading));
        rt().draw(makeText(truncFront(line.product.name, 18), 12, {350.0f, ry + 16.0f}, ui::theme::Text));
        ui::theme::drawRoundedBox(rt(), {488.0f + rx, ry + 10.0f}, {26.0f, 26.0f}, 7.0f, ui::theme::White, ui::theme::Border, 1.0f);
        rt().draw(makeText("-", 15, {496.0f + rx, ry + 13.0f}, ui::theme::PrimaryPressed));
        rt().draw(makeText(std::to_string(line.quantity), 14, {522.0f + rx, ry + 14.0f}, ui::theme::Heading));
        ui::theme::drawRoundedBox(rt(), {540.0f + rx, ry + 10.0f}, {26.0f, 26.0f}, 7.0f, ui::theme::White, ui::theme::Border, 1.0f);
        rt().draw(makeText("+", 15, {547.0f + rx, ry + 13.0f}, ui::theme::PrimaryPressed));
        rt().draw(makeText(stringutils::money(BillCalculator::lineTotal(line)), 12, {582.0f + rx, ry + 16.0f}, ui::theme::Text));
        rt().draw(makeText("x", 14, {649.0f + rx, ry + 14.0f}, ui::theme::Error));
    }
    if (lines.size() > shown)
        rt().draw(makeText("...and " + std::to_string(lines.size() - shown) + " more line(s) - all included in the bill.",
            11, {272.0f, 548.0f}, ui::theme::Muted));
    m_posBillBtn.draw(rt());
    m_posNewBtn.draw(rt());
    m_posHoldBtn.draw(rt());
    m_posResumeBtn.draw(rt());
    m_heldDrop.draw(rt());
    m_heldDrop.drawList(rt());
    rt().draw(makeText(std::to_string(lines.size()) + " line(s)", 11, {624.0f + rx, 654.0f}, ui::theme::Muted));

    // --- customer & payment card
    m_posCustomer.setPosition({cx + 20.0f, 332.0f});
    m_posCustomer.setSize({244.0f, 34.0f});
    m_posContact.setPosition({cx + 20.0f, 394.0f});
    m_posContact.setSize({244.0f, 34.0f});
    m_modeDrop.setPosition({cx + 20.0f, 444.0f});
    m_modeDrop.setSize({112.0f, 28.0f});
    m_statusDrop.setPosition({cx + 144.0f, 444.0f});
    m_statusDrop.setSize({120.0f, 28.0f});
    m_merchantDrop.setPosition({cx + 20.0f, 494.0f});
    m_merchantDrop.setSize({244.0f, 28.0f});
    rt().draw(makeText("Special % (max 8)", 11, {cx + 20.0f, 532.0f}, ui::theme::Muted));
    rt().draw(makeText("Amount received", 11, {cx + 144.0f, 532.0f}, ui::theme::Muted));
    m_posSpecialPct.setPosition({cx + 20.0f, 546.0f});
    m_posSpecialPct.setSize({112.0f, 28.0f});
    m_posAmount.setPosition({cx + 144.0f, 546.0f});
    m_posAmount.setSize({120.0f, 28.0f});
    m_posSpecialPct.draw(rt());
    m_posAmount.draw(rt());

    // --- totals card
    ui::theme::drawCard(rt(), {{cx, 584.0f}, {284.0f, 116.0f}});
    const Totals t = m_orderService.totals();
    rt().draw(makeText("Subtotal (no tax): " + pkr(t.withoutTax), 12, {cx + 20.0f, 598.0f}, ui::theme::Text));
    rt().draw(makeText("Tax: " + pkr(t.tax), 12, {cx + 20.0f, 618.0f}, ui::theme::Text));
    rt().draw(makeText("Discount: -" + pkr(t.discount), 12, {cx + 20.0f, 638.0f}, ui::theme::Text));
    rt().draw(makeText("PAYABLE", 12, {cx + 20.0f, 662.0f}, ui::theme::Muted));
    rt().draw(makeText(pkr(t.payable), 17, {cx + 170.0f, 656.0f}, ui::theme::ActiveText));

    // Open drop-down lists draw last, on top of the cards.
    m_modeDrop.drawList(rt());
    m_statusDrop.drawList(rt());
    m_merchantDrop.drawList(rt());

    if (m_showBill) drawBillOverlay();
}

void BarcodeStudioGui::drawProducts()
{
    const float rx = m_w - 1000.0f;
    rt().draw(makeText("Products", 25, {kLeft, 26.0f}, ui::theme::Heading));
    m_productsRefresh.setPosition({770.0f + rx, 22.0f});
    m_productsRefresh.draw(rt());
    ui::theme::drawCard(rt(), {{246.0f, 92.0f}, {728.0f + rx, 532.0f}});
    rt().draw(makeText("ID", 11, {272.0f, 112.0f}, ui::theme::Muted));
    rt().draw(makeText("Barcode", 11, {316.0f, 112.0f}, ui::theme::Muted));
    rt().draw(makeText("Acronym", 11, {396.0f, 112.0f}, ui::theme::Muted));
    rt().draw(makeText("Name", 11, {486.0f, 112.0f}, ui::theme::Muted));
    rt().draw(makeText("Unit", 11, {648.0f, 112.0f}, ui::theme::Muted));
    rt().draw(makeText("Price", 11, {712.0f, 112.0f}, ui::theme::Muted));
    rt().draw(makeText("Stock", 11, {872.0f + rx, 112.0f}, ui::theme::Muted));
    if (m_productsCache.empty())
    {
        rt().draw(makeText("No products found - Data/product.txt is missing or empty.", 14,
            {300.0f, 320.0f}, ui::theme::Error));
    }
    // Scrollable window of 8 rows over the full list (mouse wheel).
    const int total = static_cast<int>(m_productsCache.size());
    const int maxScroll = std::max(0, total - 8);
    const int first = std::min(m_productsScroll, maxScroll);
    const int last = std::min(total, first + 8);
    float y = 140.0f;
    for (int pi = first; pi < last; ++pi)
    {
        const Product& p = m_productsCache[static_cast<std::size_t>(pi)];
        ui::theme::drawRoundedBox(rt(), {262.0f, y}, {696.0f + rx, 48.0f}, 10.0f, ui::theme::LightPanel);
        rt().draw(makeText(std::to_string(p.id), 13, {272.0f, y + 8.0f}, ui::theme::Text));
        rt().draw(makeText(p.barcode, 13, {316.0f, y + 8.0f}, ui::theme::Text));
        rt().draw(makeText(p.acronym, 13, {396.0f, y + 8.0f}, ui::theme::Heading));
        rt().draw(makeText(truncFront(p.name, 20), 13, {486.0f, y + 8.0f}, ui::theme::Text));
        rt().draw(makeText(p.unit, 12, {648.0f, y + 8.0f}, ui::theme::Text));
        rt().draw(makeText(stringutils::money(p.unitPrice), 13, {712.0f, y + 8.0f}, ui::theme::Text));
        ui::theme::drawStatus(rt(), m_font, {856.0f + rx, y + 9.0f}, {84.0f, 26.0f},
            std::to_string(p.availableQuantity) + " left", p.availableQuantity > 0);
        rt().draw(makeText("Tax " + stringutils::money(p.taxPerUnit) + "  -  Discount " +
            stringutils::money(p.discountPerUnit) + "  -  " + p.barcodeFile,
            10, {272.0f, y + 28.0f}, ui::theme::Muted));
        y += 56.0f;
    }
    if (total > 8)  // slim scrollbar on the card's right edge
    {
        const float trackX = 958.0f + rx, trackY = 140.0f, trackH = 448.0f;
        ui::theme::drawRoundedBox(rt(), {trackX, trackY}, {6.0f, trackH}, 3.0f, ui::theme::LightPanel);
        const float thumbH = std::max(28.0f, trackH * 8.0f / static_cast<float>(total));
        const float thumbY = trackY + (trackH - thumbH) *
            (maxScroll == 0 ? 0.0f : static_cast<float>(first) / static_cast<float>(maxScroll));
        ui::theme::drawRoundedBox(rt(), {trackX, thumbY}, {6.0f, thumbH}, 3.0f, ui::theme::Accent);
    }
    rt().draw(makeText(std::to_string(total) + " product(s)  -  showing " +
        std::to_string(total == 0 ? 0 : first + 1) + "-" + std::to_string(last) +
        "  -  reading: " + shorten(m_productService.productFilePath(), 40),
        11, {272.0f, 596.0f}, ui::theme::Muted));
    if (total > 8)
    {
        m_productsPrev.setPosition({756.0f + rx, 584.0f});
        m_productsNext.setPosition({862.0f + rx, 584.0f});
        m_productsPrev.draw(rt());
        m_productsNext.draw(rt());
    }
}

void BarcodeStudioGui::drawBillOverlay()
{
    const float rx = m_w - 1000.0f;
    sf::RectangleShape dim({m_w, m_h});
    dim.setFillColor(sf::Color(20, 12, 24, 170));
    rt().draw(dim);

    const sf::FloatRect card({246.0f, 100.0f}, {500.0f + rx, 520.0f});
    ui::theme::drawCard(rt(), card);
    const Bill& b = m_lastBill.bill;
    rt().draw(makeText("Bill " + b.id, 20,
        {card.position.x + 24.0f, card.position.y + 18.0f}, ui::theme::Heading));
    rt().draw(makeText("Order " + b.orderId + "   " + b.customerName, 12,
        {card.position.x + 24.0f, card.position.y + 46.0f}, ui::theme::Text));
    rt().draw(makeText("Date " + b.billDate + "  " + b.billTime, 11,
        {card.position.x + 24.0f, card.position.y + 66.0f}, ui::theme::Muted));
    rt().draw(makeText("Product", 11,
        {card.position.x + 24.0f, card.position.y + 92.0f}, ui::theme::Muted));
    rt().draw(makeText("Qty", 11,
        {card.position.x + 196.0f, card.position.y + 92.0f}, ui::theme::Muted));
    rt().draw(makeText("Unit", 11,
        {card.position.x + 254.0f, card.position.y + 92.0f}, ui::theme::Muted));
    rt().draw(makeText("Line Disc", 11,
        {card.position.x + 356.0f, card.position.y + 92.0f}, ui::theme::Muted));
    float y = card.position.y + 116.0f;
    for (const BillDetail& d : m_lastBill.billDetail)
    {
        ui::theme::drawRoundedBox(rt(), {card.position.x + 16.0f, y},
            {card.size.x - 32.0f, 28.0f}, 6.0f, ui::theme::LightPanel);
        rt().draw(makeText(d.productAcronym, 12,
            {card.position.x + 24.0f, y + 6.0f}, ui::theme::Heading));
        rt().draw(makeText(std::to_string(d.quantity), 12,
            {card.position.x + 196.0f, y + 6.0f}, ui::theme::Text));
        rt().draw(makeText(stringutils::money(d.unitPrice), 12,
            {card.position.x + 254.0f, y + 6.0f}, ui::theme::Text));
        rt().draw(makeText(stringutils::money(d.lineDiscount), 12,
            {card.position.x + 356.0f, y + 6.0f}, ui::theme::Text));
        y += 30.0f;
        if (y > card.position.y + 300.0f) break;
    }
    y = card.position.y + 330.0f;
    rt().draw(makeText("Subtotal (no tax):", 12,
        {card.position.x + 24.0f, y}, ui::theme::Text));
    rt().draw(makeText(pkr(b.totalWithoutTax), 12,
        {card.position.x + 356.0f, y}, ui::theme::Text)); y += 18.0f;
    rt().draw(makeText("Tax:", 12, {card.position.x + 24.0f, y}, ui::theme::Text));
    rt().draw(makeText(pkr(b.totalTax), 12,
        {card.position.x + 356.0f, y}, ui::theme::Text)); y += 18.0f;
    rt().draw(makeText("Product discount:", 12,
        {card.position.x + 24.0f, y}, ui::theme::Text));
    rt().draw(makeText("-" + pkr(b.totalDiscount), 12,
        {card.position.x + 356.0f, y}, ui::theme::Text)); y += 18.0f;
    rt().draw(makeText("Seasonal discount:", 12,
        {card.position.x + 24.0f, y}, ui::theme::Text));
    rt().draw(makeText("-" + pkr(b.seasonalDiscount), 12,
        {card.position.x + 356.0f, y}, ui::theme::Text)); y += 18.0f;
    rt().draw(makeText("Special discount:", 12,
        {card.position.x + 24.0f, y}, ui::theme::Text));
    rt().draw(makeText("-" + pkr(b.specialDiscount), 12,
        {card.position.x + 356.0f, y}, ui::theme::Text)); y += 22.0f;
    rt().draw(makeText("PAYABLE:", 13,
        {card.position.x + 24.0f, y}, ui::theme::Muted));
    rt().draw(makeText(pkr(b.totalPayable), 16,
        {card.position.x + 320.0f, y - 2.0f}, ui::theme::ActiveText)); y += 22.0f;
    rt().draw(makeText("Received:", 12,
        {card.position.x + 24.0f, y}, ui::theme::Text));
    rt().draw(makeText(pkr(b.amountReceived), 12,
        {card.position.x + 356.0f, y}, ui::theme::Text)); y += 18.0f;
    rt().draw(makeText("Balance:", 12,
        {card.position.x + 24.0f, y}, ui::theme::Text));
    rt().draw(makeText(pkr(b.balanceReturned), 12,
        {card.position.x + 356.0f, y}, ui::theme::Text)); y += 18.0f;
    rt().draw(makeText("Status: " + b.paymentStatus, 12,
        {card.position.x + 24.0f, y}, ui::theme::Muted));

    m_billCloseBtn.setPosition({card.position.x + 24.0f, card.position.y + 470.0f});
    m_printPdfBtn.setPosition({card.position.x + 236.0f, card.position.y + 470.0f});
    m_printPdfBtn.setLabel("Print as PDF");
    m_billCloseBtn.draw(rt());
    m_printPdfBtn.draw(rt());
    if (!m_printMessage.empty())
        rt().draw(makeText(m_printMessage, 11,
            {card.position.x + 24.0f, card.position.y + 508.0f},
            m_printMessage.find("saved") != std::string::npos
                ? ui::theme::Success : ui::theme::Error));
}