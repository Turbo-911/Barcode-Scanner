#pragma once
#include <string>

// Domain entity: ONE row of Data/bill.txt. The file holds one row per ordered
// product; all rows of one bill share the same Bill Id and the same
// order-level totals. Pure data (SRP).
struct Bill
{
    std::string id;              // 9 digits
    std::string customerName;    // stored with '_' instead of spaces in the file
    std::string orderId;         // from Data/order.txt
    std::string productAcronym;
    double unitPrice = 0.0;      // without tax
    double discountPerUnit = 0.0;
    int quantity = 0;
    double totalWithoutTax = 0.0; // order-level totals (same on every row)
    double totalTax = 0.0;
    double totalDiscount = 0.0;
    double totalPayable = 0.0;
    std::string paymentStatus;   // = order status from Data/order.txt
};
