#pragma once

#include "ui/Widgets.h"
#include "ui/Theme.h"
#include "common/common.h"
#include "domain/product.h"
#include "persistence/i_product_repo.h"
#include "persistence/i_order_repo.h"
#include "persistence/i_bill_repo.h"
#include "services/product_service.h"
#include "services/order_service.h"

#include <optional>

#include <SFML/Graphics.hpp>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// BarcodeStudioGui — SFML 3 GUI for the Chili Mili barcode POS.
// Screens: Generator, File Scanner, Camera Scanner, POS Order,
// Products, Self-Test. Phase 1 screens only CALL the existing core
// functions; Phase 2 screens call the POS services (OrderService /
// ProductService), which own all file and money logic (SRP).
// It only CALLS the existing core functions (isValidCode, renderBarcode,
// loadBMP/saveBMP, decodeBarcode, captureFrame); it changes none of them.
class BarcodeStudioGui
{
public:
    BarcodeStudioGui();
    ~BarcodeStudioGui();
    int run();
    bool screenshot(const std::string& path, int view = -1);  // draw one frame and save it (dev/QA)

private:
    enum class View { Generator, FileScanner, CameraScanner, PosOrder, Products, SelfTest };

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
    ui::PinkButton m_cameraToggleBtn;  // Start / Stop
    ui::PinkButton m_scanNextBtn;      // Scan Another

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

    // ---- Phase 2 POS (Chili Mili) ----
    // Composition root: the GUI wires repositories -> services once.
    ProductFileRepository m_productRepo;
    OrderFileRepository m_orderRepo;
    OrderDetailFileRepository m_detailRepo;
    BillFileRepository m_billRepo;
    ProductService m_productService;
    OrderService m_orderService;

    ui::PinkTextBox m_posBarcode, m_posQty, m_posCustomer, m_posContact;
    ui::PinkButton m_posDescBtn, m_posAddBtn, m_posCamBtn;
    ui::PinkDropdown m_modeDrop, m_statusDrop, m_merchantDrop;
    int m_modeIdxCache{0};  // last payment mode (merchant list follows it)
    ui::PinkButton m_posBillBtn, m_posNewBtn, m_productsRefresh, m_billCloseBtn;
    ui::PinkButton m_productsPrev, m_productsNext;
    int m_productsScroll{0};  // first visible product row (mouse-wheel scrolling)
    std::optional<Product> m_descProduct;
    std::string m_lastScannedCode;  // last code looked up on the POS screen      // last product described (scan/lookup)
    std::vector<Product> m_productsCache;
    std::string m_posMessage{"Scan a barcode or type it, set qty, then Add to Order."};
    bool m_posMessageOk{true};
    CompletedBill m_lastBill;
    bool m_showBill{false};
    bool m_fullscreen{false};
    // Responsive layout: the UI fills the whole window (no letterbox
    // bars). Layout height stays 680 units; the width grows with the
    // window (m_w >= 1000) and right-hand panels anchor to m_w.
    float m_w{1000.0f};
    float m_h{680.0f};
    float m_renderScale{1.0f};  // layout units -> window pixels
    void refreshProducts();
    void posDescribe();
    void posAdd();
    void posCheckout();
    void posNewOrder();
    void drawBillOverlay();
    void drawDescriptionCard(const sf::FloatRect& card);
    void drawPosCameraCard(const sf::FloatRect& card);
    void applyView();
    void toggleFullscreen();

    // ---- v3 "alive" layer: particles, mascot, transitions, animations ----
    struct Particle { sf::Vector2f pos, vel; float size, phase, life, maxLife; int kind; };
    std::vector<Particle> m_ambient;   // slow floating hearts / sparkles
    std::vector<Particle> m_trail;     // sparkles that follow the mouse
    std::vector<Particle> m_burst;     // celebration explosion on success
    float m_elapsed{0.0f};
    float m_transition{1.0f};          // legacy (kept for screenshot mode)
    sf::RenderTexture m_contentTexture;
    // Closable sidebar: rendered to its own texture and slid away;
    // the content then centres itself on the single screen.
    sf::RenderTexture m_sidebarTexture;
    sf::RenderTarget* m_sidebarRt{nullptr};
    sf::RenderTarget& sidebarRt() { return *m_sidebarRt; }
    bool m_sidebarOpen{true};
    float m_sidebarClosed{0.0f};       // 0 = open, 1 = closed (animated)
    sf::FloatRect sidebarToggleRect() const;
    void drawSidebarToggle();
    sf::Vector2f contentMouse() const
    {
        return {m_mouse.x + 110.0f * m_sidebarClosed, m_mouse.y};
    }
    sf::RenderTarget* m_rt{nullptr};
    sf::RenderTarget& rt() { return *m_rt; }
    float m_shake{0.0f};               // invalid-input shake timer
    std::array<float, 6> m_tilePop{};  // per-digit pop animation
    std::size_t m_prevCodeLen{0};
    float m_testRunTime{99.0f};        // time since self-test ran (stagger)
    float m_mascotHappy{0.0f};         // >0 while mascot celebrates
    std::string m_mascotText{"Let's create a\nbarcode!"};
    sf::Vector2f m_mouse{0.0f, 0.0f};
    void initParticles();
    void updateParticles(float dt);
    void drawParticles();
    void drawBursts();
    void spawnBurst(const sf::Vector2f& centre, int count = 26);
    void celebrate(const std::string& bubble, const sf::Vector2f& burstAt);
    void drawMascot();
    void switchView(View next);
    void drawDigitTiles(float shakeX);

    bool anyTextFocused() const;
    void handleEvents();
    void handleEnterKey();
    void update(float deltaSeconds);
    void draw();
    void drawSidebar();
    void drawGenerator();
    void drawFileScanner();
    void drawCameraScanner();
    void drawPosOrder();
    void drawProducts();
    void drawSelfTest();
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
};
