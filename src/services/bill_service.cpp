#include "services/bill_service.h"

std::vector<Bill> BillService::buildRows(const Order& order,
    const std::vector<CartLine>& lines, const Totals& totals,
    const std::string& billId) const
{
    std::vector<Bill> rows;
    for (const CartLine& line : lines)
    {
        Bill b;
        b.id = billId;
        b.customerName = order.customerName;
        b.orderId = order.id;
        b.productAcronym = line.product.acronym;
        b.unitPrice = line.product.unitPrice;
        b.discountPerUnit = line.product.discountPerUnit;
        b.quantity = line.quantity;
        b.totalWithoutTax = totals.withoutTax;
        b.totalTax = totals.tax;
        b.totalDiscount = totals.discount;
        b.totalPayable = totals.payable;
        b.paymentStatus = order.status;
        rows.push_back(b);
    }
    return rows;
}

bool BillService::appendRows(const std::vector<Bill>& rows)
{
    bool written = true;
    for (const Bill& b : rows) written = m_bills.append(b) && written;
    return written;
}
