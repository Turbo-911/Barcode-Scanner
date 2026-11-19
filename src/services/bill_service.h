#pragma once
#include "domain/bill.h"
#include "domain/order.h"
#include "persistence/i_bill_repo.h"
#include "bill_calculator.h"
#include <string>
#include <vector>

// BillService - everything to do with BUILDING and SAVING a bill
// (SRP): turning a checked-out order into its Data/bill.txt rows (one row
// per cart line, all sharing one Bill Id and the order totals) and
// appending those rows through IBillRepository.
// OrderService orchestrates checkout; it delegates the bill part to
// this service, so the bill rules live in exactly one place.
class BillService
{
public:
    explicit BillService(IBillRepository& bills) : m_bills(bills) {}

    // Build the bill rows for an order (pure - no file access).
    std::vector<Bill> buildRows(const Order& order,
        const std::vector<CartLine>& lines, const Totals& totals,
        const std::string& billId) const;

    // Append every row; false if any write failed.
    bool appendRows(const std::vector<Bill>& rows);

    // Number of bills stored (rows sharing a Bill Id count once).
    int billCount() { return m_bills.count(); }

private:
    IBillRepository& m_bills;
};
