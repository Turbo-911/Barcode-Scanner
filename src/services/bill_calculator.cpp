#include "services/bill_calculator.h"

#include <algorithm>
#include <cmath>

double BillCalculator::round2(double value)
{
    return std::round(value * 100.0) / 100.0;
}

Totals BillCalculator::calculate(const std::vector<CartLine>& lines)
{
    Totals t;
    for (const CartLine& line : lines)
    {
        t.withoutTax += line.product.unitPrice * line.quantity;
        t.tax += line.product.taxPerUnit * line.quantity;
        t.discount += line.product.discountPerUnit * line.quantity;
    }
    t.withoutTax = round2(t.withoutTax);
    t.tax = round2(t.tax);
    t.discount = round2(t.discount);
    t.payable = round2(t.withoutTax + t.tax - t.discount);
    return t;
}

double BillCalculator::lineTotal(const CartLine& line)
{
    return round2((line.product.unitPrice + line.product.taxPerUnit
        - line.product.discountPerUnit) * line.quantity);
}

double BillCalculator::payableAfterDiscounts(const Totals& totals,
    double seasonalDiscount, double specialDiscount)
{
    const double payable = totals.payable - round2(seasonalDiscount) - round2(specialDiscount);
    return std::max(0.0, round2(payable));
}
