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
    double seasonal = 0.0;
    double special = 0.0;
    cappedDiscounts(totals, seasonalDiscount, specialDiscount, seasonal, special);
    const double payable = totals.payable - seasonal - special;
    return std::max(0.0, round2(payable));
}

// v38 (D-10/D-22): cap the COMBINED seasonal + special discount at 50%
// of the pre-discount payable. The cap trims the special discount
// first (it is discretionary), then the seasonal, so the two effective
// amounts always sum to the discount actually taken.
void BillCalculator::cappedDiscounts(const Totals& totals,
    double seasonalDiscount, double specialDiscount,
    double& seasonalOut, double& specialOut)
{
    double seasonal = round2(seasonalDiscount);
    double special = round2(specialDiscount);
    if (seasonal < 0.0) seasonal = 0.0;
    if (special < 0.0) special = 0.0;
    const double cap = round2(totals.payable * 0.5);
    const double combined = round2(seasonal + special);
    if (combined > cap)
    {
        double excess = round2(combined - cap);
        const double trimSpecial = special < excess ? special : excess;
        special = round2(special - trimSpecial);
        excess = round2(excess - trimSpecial);
        seasonal = round2(seasonal - excess);
    }
    seasonalOut = seasonal;
    specialOut = special;
}
