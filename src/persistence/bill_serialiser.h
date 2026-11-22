#pragma once
#include "domain/bill.h"
#include <optional>
#include <string>

// BillSerialiser - ONE Bill <-> ONE Data/bill.txt line (SRP), matching
// requirement 8:
//   billId customerName orderId totalWithoutTax totalTax totalDiscount
//   totalPayable amountReceived balanceReturned status seasonalDiscount
//   specialDiscount billDate billTime #
class BillSerialiser
{
public:
    static std::optional<Bill> parse(const std::string& line);
    static std::string format(const Bill& bill);
};