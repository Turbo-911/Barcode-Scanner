#pragma once
#include "domain/bill.h"
#include <optional>
#include <string>

// BillSerialiser — ONE Bill <-> ONE Data/bill.txt line (SRP):
//   billId customerName orderId acronym unitPrice discountPerUnit quantity
//   totalWithoutTax totalTax totalDiscount totalPayable paymentStatus #
class BillSerialiser
{
public:
    static std::optional<Bill> parse(const std::string& line);
    static std::string format(const Bill& bill);
};
