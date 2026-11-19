#pragma once
#include <string>

// Domain entity: ONE order, mirroring one row of Data/order.txt.
// Pure data (SRP).
struct Order
{
    std::string id;          // 8 digits, last two = day of the month of the order
    std::string date;        // YYYY-MM-DD (single token)
    std::string time;        // HH:MM:SS (single token)
    std::string customerName;   // stored with '_' instead of spaces in the file
    std::string customerContact; // optional; "-" in the file when not given
    std::string status;      // in-progress | on-hold | paid | archived (see enums.h)
    double paymentAmount = 0.0; // total payable at the time the bill is generated
    std::string paymentMode;    // cash | card | wallet (see enums.h)
    std::string paymentMerchant; // e.g. ABL, JazzCash; "-" when not applicable
};

// Domain entity: ONE row of Data/orderDetail.txt - a product + quantity
// recorded against an order id. Part of the Order aggregate, so it lives
// in this header with Order. Pure data (SRP).
struct OrderDetail
{
    std::string orderId;
    int productId = 0;
    std::string productBarcode;
    int quantity = 0;
};
