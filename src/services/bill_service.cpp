#include "services/bill_service.h"

#include <cmath>
#include <optional>

// BillService member definitions (declared in bill_service.h).

std::optional<Bill> BillService::buildBill(const Order& order, const Totals& totals,
    const std::string& billId, double seasonalDiscount,
    double specialDiscount, double amountReceived) const
{
    Bill b;
    b.id = billId;
    b.customerName = order.customerName;
    b.orderId = order.id;
    b.totalWithoutTax = BillCalculator::round2(totals.withoutTax);
    b.totalTax = BillCalculator::round2(totals.tax);
    b.totalDiscount = BillCalculator::round2(totals.discount);
    b.seasonalDiscount = BillCalculator::round2(seasonalDiscount);
    b.specialDiscount = BillCalculator::round2(specialDiscount);
    // THE one payable formula (requirement 8, D-31/D-32): what the
    // customer owes after product discounts and the two bill-level
    // discounts, rounded to whole paisa, never below zero.
    b.totalPayable = BillCalculator::payableAfterDiscounts(totals,
        seasonalDiscount, specialDiscount);
    // The written fields must sum to the written payable: withoutTax +
    // tax - discount - seasonal - special == payable. Everything above
    // is rounded to 2 decimals, so this holds exactly; if it ever does
    // not, refuse the bill instead of writing a wrong one (D-32).
    const double recomputed = BillCalculator::round2(
        b.totalWithoutTax + b.totalTax - b.totalDiscount
        - b.seasonalDiscount - b.specialDiscount);
    if (std::fabs(recomputed - b.totalPayable) > 0.005)
        return std::nullopt;
    b.amountReceived = BillCalculator::round2(amountReceived);
    // Balance can never be negative: underpayment is not change.
    b.balanceReturned = b.amountReceived > b.totalPayable
        ? BillCalculator::round2(b.amountReceived - b.totalPayable) : 0.0;
    b.paymentStatus = order.status;  // bill status mirrors the order status
    b.billDate = order.date;
    b.billTime = order.time;
    return b;
}

std::vector<BillDetail> BillService::buildDetails(
    const std::vector<CartLine>& lines, const std::string& billId) const
{
    std::vector<BillDetail> out;
    for (const CartLine& line : lines)
    {
        BillDetail d;
        d.billId = billId;
        d.productAcronym = line.product.acronym;
        d.unitPrice = line.product.unitPrice;  // without tax
        d.quantity = line.quantity;
        // Requirement 9: the LINE's total discount (per-unit x quantity).
        d.lineDiscount = BillCalculator::round2(line.product.discountPerUnit * line.quantity);
        out.push_back(d);
    }
    return out;
}

bool BillService::appendBill(const Bill& bill,
    const std::vector<BillDetail>& details)
{
    bool written = m_bills.appendBill(bill);
    for (const BillDetail& d : details)
        written = m_details.append(d) && written;
    return written;
}
