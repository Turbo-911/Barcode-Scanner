#include "services/bill_service.h"

// BillService member definitions (declared in bill_service.h).

Bill BillService::buildBill(const Order& order, const Totals& totals,
    const std::string& billId, double seasonalDiscount,
    double specialDiscount, double amountReceived) const
{
    Bill b;
    b.id = billId;
    b.customerName = order.customerName;
    b.orderId = order.id;
    b.totalWithoutTax = totals.withoutTax;
    b.totalTax = totals.tax;
    b.totalDiscount = totals.discount;
    b.seasonalDiscount = seasonalDiscount;
    b.specialDiscount = specialDiscount;
    // The one payable formula (requirement 8): what the customer owes
    // after product discounts and the two bill-level discounts.
    b.totalPayable = totals.withoutTax + totals.tax - totals.discount
        - seasonalDiscount - specialDiscount;
    if (b.totalPayable < 0.0) b.totalPayable = 0.0;
    b.amountReceived = amountReceived;
    // Balance can never be negative: underpayment is not change.
    b.balanceReturned = amountReceived > b.totalPayable
        ? amountReceived - b.totalPayable : 0.0;
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
        d.lineDiscount = line.product.discountPerUnit * line.quantity;
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
