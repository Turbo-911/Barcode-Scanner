#pragma once
#include <string>

// Domain entity: ONE product, mirroring one row of Data/product.txt.
// Pure data - no file, GUI or pricing logic lives here (SRP).
struct Product
{
    int id = 0;                    // product id
    std::string barcode;           // 6-digit ITF barcode, first 2 = categoryId
    std::string acronym;           // max 8 characters
    std::string name;              // stored with '_' instead of spaces in the file
    std::string unit;              // "count" or "weight"
    double unitPrice = 0.0;        // PKR, without tax
    double taxPerUnit = 0.0;       // PKR amount per unit
    double discountPerUnit = 0.0;  // PKR per unit
    int availableQuantity = 0;
    std::string barcodeFile;       // barcode file name and path (single token)
    std::string categoryId;        // 2-digit category id (first 2 digits of barcode)
};