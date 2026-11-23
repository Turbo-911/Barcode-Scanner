#pragma once

#include "ui/Widgets.h"
#include "ui/Theme.h"
#include "common/common.h"
#include "domain/product.h"
#include "domain/order.h"
#include "domain/bill.h"
#include "domain/enums.h"
#include "persistence/i_product_repo.h"
#include "persistence/i_order_repo.h"
#include "persistence/i_bill_repo.h"
#include "persistence/i_bill_detail_repo.h"
#include "persistence/i_category_repo.h"
#include "persistence/i_seasons_repo.h"
#include "services/product_service.h"
#include "services/order_service.h"
#include "services/category_service.h"
#include "services/seasons_service.h"
#include "services/report_service.h"

#include <optional>

#include <SFML/Graphics.hpp>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// BarcodeStudioGui - SFML 3 GUI for the Chili Mili barcode POS.
// Screens: Generator, File Scanner, Camera Scanner, POS Order, Products,
// Self-Test, plus the manager-only screens added in Phase 3
// (Categories, Seasons, Inventory, ProductEdit, Reports) and the
// cashier screens POS Held Orders / Order Edit / Bill Print.
// The GUI only CALLS services (ProductService / OrderService /
// CategoryService / SeasonsService / ReportService); those services own
// every rule and every file write (SRP).
class BarcodeStudioGui
{
public:
    BarcodeStudioGui();
    ~BarcodeStudioGui();
    int run();
    bool screenshot(const std::string& path, int view = -1);

private:
    enum class View
    {
        Generator, FileScanner, CameraScanner, PosOrder, Products, SelfTest,
        Categories, Seasons, Inventory, ProductEdit, Reports,
        HeldOrders, OrderEdit, BillPrint
    };

    sf::RenderWindow m_window;
    sf::Font m_font;
    View m_view;
    sf::Clock m_clock;

    // Generator widgets
    ui::PinkTextBox m_codeInput;
    ui::PinkTextBox m_fileNameInput;
    ui::PinkButton m_generateRandom;
    ui::PinkButton m_generate;
    std::string m_lastAutoFileName;

    // File scanner widgets
    ui::PinkTextBox m_pathInput;
    ui::PinkButton m_browse;
    ui::PinkButton m_decode;
    bool m_decodeAttempted;
    bool m_decodeOk;

    // Camera widgets
    ui::PinkButton m_cameraToggleBtn;
    ui::PinkButton m_scanNextBtn;
    ui::PinkButton m_camAddBtn;
    ui::PinkButton m_fileAddBtn;

    // Self-test widgets
    ui::PinkButton m_runTest;
    struct TestRow { std::string code, result; bool ok; };
    std::vector<TestRow> m_testRows;
    int m_testPassed;

    ui::ToastNotification m_toast;

    // Barcode preview (generator + file scanner share it)
    sf::Texture m_previewTexture;
    bool m_hasPreview;
    std::string m_previewPath;
    std::string m_decoded;
    Image m_loadedImage;

    // Camera worker thread (UI never blocks on the camera)
    std::thread m_cameraThread;
    std::mutex m_cameraMutex;
    std::atomic<bool> m_cameraRunning{false};
    std::atomic<bool> m_cameraPaused{false};
    std::atomic<bool> m_scanLocked{false};
    std::atomic<int> m_scanGeneration{0};
    std::string m_cameraStatus;
    Image m_latestCameraFrame;
    bool m_newCameraFrameReady{false};
    std::string m_scannedCode;
    bool m_newCodeScanned{false};
    std::atomic<int> m_cameraAttempts{0};
    std::atomic<int> m_consistentCount{0};
    std::string m_cameraStatusUi{"Standby"};

    sf::Texture m_cameraTexture;
    std::vector<std::uint8_t> m_cameraRgba;
    bool m_hasCameraFrame{false};

    sf::Texture m_generatedBarcodeTexture;
    bool m_hasGeneratedBarcode{false};
    std::string m_generatedBarcodePath;

    // ---- Repositories (composition root) ----
    ProductFileRepository m_productRepo;
    OrderFileRepository m_orderRepo;
    OrderDetailFileRepository m_detailRepo;
    BillFileRepository m_billRepo;
    BillDetailFileRepository m_billDetailRepo;
    CategoryFileRepository m_categoryRepo;
    SeasonsFileRepository m_seasonsRepo;

    // ---- Services ----
    CategoryService m_categoryService;
    SeasonsService m_seasonsService;
    ProductService m_productService;
    OrderService m_orderService;
    ReportService m_reportService;

    // ---- POS (existing) ----
    ui::PinkTextBox m_posBarcode, m_posQty, m_posCustomer, m_posContact;
    ui::PinkButton m_posAddBtn, m_posCamBtn;
    ui::PinkDropdown m_modeDrop, m_statusDrop, m_merchantDrop;
    int m_modeIdxCache{0};
    ui::PinkButton m_posBillBtn, m_posNewBtn, m_productsRefresh, m_billCloseBtn;
    ui::PinkButton m_productsPrev, m_productsNext;
    int m_productsScroll{0};
    std::optional<Product> m_descProduct;
    std::string m_lastScannedCode;
    std::string m_autoDescCode;
    std::vector<Product> m_productsCache;
    std::string m_posMessage{"Scan an item to add it, or type a barcode, set qty, then Add to Order."};
    bool m_posMessageOk{true};
    CompletedBill m_lastBill;
    bool m_showBill{false};
    bool m_fullscreen{false};

    // ---- Phase 3: role, POS extras, manager state ----
    UserRole m_role{UserRole::Cashier};
    float m_roleSlide{0.0f};  // role slider thumb: 0 = Cashier end, 1 = Manager end

    // POS extras (special discount + amount received + hold + resume + print)
    ui::PinkTextBox m_posSpecialPct;   // 0..8 (clamped)
    ui::PinkTextBox m_posAmount;       // amount received (digits / dot)
    ui::PinkButton m_posHoldBtn;
    ui::PinkButton m_posResumeBtn;
    ui::PinkDropdown m_heldDrop;
    std::vector<std::string> m_heldIds;
    std::string m_heldMessage;

    // Manager - Categories screen
    ui::PinkTextBox m_catIdInput, m_catNameInput, m_catAcronymInput;
    ui::PinkButton m_catAddBtn, m_catBackBtn;
    std::string m_catMessage;
    std::vector<Category> m_categoriesCache;

    // Manager - Seasons screen
    ui::PinkTextBox m_seasonStartInput, m_seasonEndInput, m_seasonPctInput;
    ui::PinkButton m_seasonAddBtn, m_seasonToggleBtn, m_seasonBackBtn;
    std::string m_seasonMessage;
    int m_seasonSelectedIdx{-1};
    std::vector<Season> m_seasonsCache;

    // Manager - Inventory screen
    ui::PinkTextBox m_invSearchInput, m_invQtyInput;
    ui::PinkButton m_invSetBtn, m_invBackBtn;
    std::string m_invMessage;
    int m_invSelectedIdx{-1};
    int m_invScroll{0};

    // Manager - Product edit screen
    ui::PinkTextBox m_peIdInput, m_peNameInput, m_peAcronymInput,
        m_pePriceInput, m_peTaxInput, m_peDiscountInput, m_peQtyInput;
    ui::PinkDropdown m_peUnitDrop, m_peCategoryDrop;
    ui::PinkButton m_peSaveBtn, m_peBackBtn, m_peBarcodeBtn;
    std::string m_peMessage;
    int m_peLoadedId{-1};  // product loaded in the edit form (its list row is highlighted)
    std::string m_peGeneratedBarcode;

    // Manager - Reports screen
    ui::PinkDropdown m_reportDrop;
    ui::PinkTextBox m_reportFromInput, m_reportToInput;
    ui::PinkButton m_reportRunBtn, m_reportBackBtn;
    ui::PinkButton m_reportPrevBtn, m_reportNextBtn;
    ReportService::Table m_reportTable;
    int m_reportScroll{0};
    std::string m_reportMessage;

    // Cashier - Held Orders screen
    ui::PinkButton m_heldResumeBtn, m_heldDeleteBtn, m_heldBackBtn;
    ui::PinkButton m_heldPrevBtn, m_heldNextBtn;
    std::string m_heldScreenMessage;
    int m_heldScroll{0};
    std::vector<Order> m_heldCache;

    // Cashier/Manager - Order Edit screen (edit a paid order within 7 days)
    ui::PinkTextBox m_oeOrderIdInput, m_oeCustomerInput, m_oeContactInput;
    ui::PinkButton m_oeLoadBtn, m_oeApproveBtn, m_oeSaveBtn, m_oeBackBtn;
    std::string m_oeMessage;
    bool m_oeManagerApproved{false};
    std::string m_oeLoadedId;  // the order actually loaded in Order Edit (Save acts on this, not the id box)
    std::vector<std::pair<Product, int>> m_oeLines;

    // Bill Print screen (or overlay)
    ui::PinkButton m_printPdfBtn, m_printCloseBtn;
    std::string m_printMessage;

    // Responsive layout
    float m_w{1000.0f};
    float m_h{680.0f};
    float m_renderScale{1.0f};

    // ---- POS actions (existing + new) ----
    bool onEnterKey();
    void refreshProducts();
    void posDescribe();
    void posAdd();
    bool posAddOne(const std::string& barcode);
    void addDecodedToOrder();
    void posCheckout();
    void posNewOrder();
    void posHold();
    void posResume();
    void posPrintPdf();
    void drawBillOverlay();
    void drawDescriptionCard(const sf::FloatRect& card);
    void drawPosCameraCard(const sf::FloatRect& card);
    void applyView();
    void toggleFullscreen();

    // ---- Manager actions ----
    void refreshCategories();
    void categoryAdd();
    void refreshSeasons();
    void seasonAdd();
    void seasonToggle();
    void refreshInventory();
    void inventorySet();
    void productEditLoad();
    void productEditSave();
    void productEditGenerateBarcode();
    void runReport();
    void drawReportTable(const sf::FloatRect& card);

    // ---- Cashier actions (held orders) ----
    void refreshHeldOrders();
    void applyResumedHeader(const Order& o);
    void heldResumeSelected();
    void heldDeleteSelected();

    // ---- Manager/cashier actions (order edit) ----
    void orderEditLoad();
    void orderEditApprove();
    void orderEditSave();

    // ---- Draw methods (existing) ----
    void drawSidebar();
    void drawGenerator();
    void drawFileScanner();
    void drawCameraScanner();
    void drawPosOrder();
    void drawProducts();
    void drawSelfTest();

    // ---- Draw methods (new) ----
    void drawCategories();
    void drawSeasons();
    void drawInventory();
    void drawProductEdit();
    void drawReports();
    void drawHeldOrders();
    void drawOrderEdit();
    void drawBillPrint();
    void drawManagerPill(const sf::FloatRect& bounds, const std::string& text, bool ok);

    // ---- Shared ----
    void drawPreview(const sf::FloatRect& bounds, const sf::Texture& texture, bool has);
    void setPreviewFromImage(const Image& image);
    void generateBarcode();
    void browseFile();
    void decodeFile();
    void runSelfTest();
    void showToast(const std::string& message, bool success);
    sf::Text makeText(const std::string& value, unsigned int size, const sf::Vector2f& position,
        sf::Color color = ui::theme::Text) const;
    static std::string chooseFile();
    static std::string stripQuotes(std::string s);

    void startCamera();
    void stopCamera();
    void cameraWorkerLoop();
    void updateCameraTexture(const Image& frame);
    void onBarcodeScanned(const std::string& code);
    void resetScan();
    void resetScanState();

    // ---- v3 "alive" layer (unchanged) ----
    struct Particle { sf::Vector2f pos, vel; float size, phase, life, maxLife; int kind; };
    std::vector<Particle> m_ambient;
    std::vector<Particle> m_trail;
    std::vector<Particle> m_burst;
    float m_elapsed{0.0f};
    float m_transition{1.0f};
    sf::RenderTexture m_contentTexture;
    sf::RenderTexture m_sidebarTexture;
    sf::RenderTarget* m_sidebarRt{nullptr};
    sf::RenderTarget& sidebarRt() { return *m_sidebarRt; }
    bool m_sidebarOpen{true};
    float m_sidebarClosed{0.0f};
    sf::FloatRect sidebarToggleRect() const;
    void drawSidebarToggle();
    sf::Vector2f contentMouse() const
    {
        return {m_mouse.x + 110.0f * m_sidebarClosed, m_mouse.y};
    }
    sf::RenderTarget* m_rt{nullptr};
    sf::RenderTarget& rt() { return *m_rt; }
    float m_shake{0.0f};
    std::array<float, 6> m_tilePop{};
    std::size_t m_prevCodeLen{0};
    float m_testRunTime{99.0f};
    sf::Vector2f m_mouse{0.0f, 0.0f};
    void initParticles();
    void updateParticles(float dt);
    void drawParticles();
    void drawBursts();
    void spawnBurst(const sf::Vector2f& centre, int count = 26);
    void celebrate(const std::string& bubble, const sf::Vector2f& burstAt);
    void switchView(View next);
    void drawDigitTiles(float shakeX);

    bool anyTextFocused() const;
    void handleEvents();
    void update(float deltaSeconds);
    void draw();
};