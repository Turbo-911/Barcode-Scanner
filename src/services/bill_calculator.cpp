#include "services/bill_calculator.h"

Totals BillCalculator::calculate(const std::vector<CartLine>& lines)
{
    Totals t;
    for (const CartLine& line : lines)
    {
        t.withoutTax += line.product.unitPrice * line.quantity;
        t.tax += line.product.taxPerUnit * line.quantity;
        t.discount += line.product.discountPerUnit * line.quantity;
    }
    t.payable = t.withoutTax + t.tax - t.discount;
    return t;
}

double BillCalculator::lineTotal(const CartLine& line)
{
    return (line.product.unitPrice + line.product.taxPerUnit
        - line.product.discountPerUnit) * line.quantity;
}
