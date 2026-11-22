#pragma once
#include "domain/bill.h"
#include "domain/order.h"
#include "persistence/i_bill_detail_repo.h"
#include "persistence/i_bill_repo.h"
#include "bill_calculator.h"
#include <string>
#include <vector>

// BillService - everything to do with BUILDING and SAVING a bill (SRP).
// Requirements 8 and 9 are separated here: a single Bill row (bill-level
// totals and payment info) is written to Data/bill.txt, and one
// BillDetail row per product is written to Data/BillDetail.txt.
// OrderService orchestrates checkout; it delegates the bill part here.
class BillService
{
public:
    BillService(IBillRepository& bills, IBillDetailRepository& details)
        : m_bills(bills), m_details(details) {}

    // Build the ONE Bill row for a checked-out order. The seasonal and
    // special discounts are rupee AMOUNTS already applied to the total;
    // the caller computes them (see OrderService::checkout).
    Bill buildBill(const Order& order, const Totals& totals, const std::string& billId,
        double seasonalDiscount, double specialDiscount,
        double amountReceived) const;

    // Build the per-line BillDetail rows for the same order.
    std::vector<BillDetail> buildDetails(const std::vector<CartLine>& lines,
        const std::string& billId) const;

    // Append one bill and its detail rows. Returns false if any write failed.
    bool appendBill(const Bill& bill, const std::vector<BillDetail>& details);

    int billCount() { return m_bills.count(); }

private:
    IBillRepository& m_bills;
    IBillDetailRepository& m_details;
};
