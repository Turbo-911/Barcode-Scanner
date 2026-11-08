#pragma once

class App {
public:
    int run();

private:
    void showMenu() const;

    void generateBarcode();   // 6-digit number to barcode image (shown + saved as BMP).
    void readFromFile();      // barcode BMP file to 6-digit number.
    void scanWithCamera();    // webcam frames to 6-digit number.
    void selfTest();          // encode to decode.
};
