#pragma once
#include <string>

// Domain entity: ONE row of Data/bill.txt - one row PER BILL now.
// The per-product lines live in BillDetail (Data/BillDetail.txt), so a
// bill has exactly one Bill row and any number of BillDetail rows that
// share its billId. All money fields are stored as plain numbers; the
// seasonal and special discounts are DOLLAR/rupee amounts, not percentages
// (the percentages were already applied by the time the row is written).
// Pure data (SRP).
struct Bill
{
    std::string id;                // 9 digits, unique across the file
    std::string customerName;      // stored with '_' instead of spaces
    std::string orderId;           // from Data/order.txt
    double totalWithoutTax = 0.0;  // sum of (unitPrice * qty)
    double totalTax = 0.0;         // sum of (taxPerUnit * qty)
    double totalDiscount = 0.0;    // sum of per-line discounts (product discounts)
    double totalPayable = 0.0;     // subtotal + tax - discount - seasonal - special
    double amountReceived = 0.0;   // money handed over by the customer
    double balanceReturned = 0.0;  // amountReceived - totalPayable (never negative)
    std::string paymentStatus;     // = order status from Data/order.txt
    double seasonalDiscount = 0.0; // rupee amount from the active season
    double specialDiscount = 0.0;  // rupee amount from the special (<=8%) rate
    std::string billDate;          // YYYY-MM-DD
    std::string billTime;          // HH:MM:SS
};

// Domain entity: ONE row of Data/BillDetail.txt - a product included in a
// bill. Many BillDetail rows share one Bill's billId. Pure data (SRP).
struct BillDetail
{
    std::string billId;            // from Data/bill.txt
    std::string productAcronym;    // product acronym from Data/product.txt
    double unitPrice = 0.0;        // per unit, WITHOUT tax
    int quantity = 0;
    double lineDiscount = 0.0;     // total discount on this line (perUnit * qty)
};