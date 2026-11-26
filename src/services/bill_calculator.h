#pragma once
#include "domain/product.h"
#include <vector>

// One line of a bill being built: a product and how many are being bought.
struct CartLine
{
    Product product;
    int quantity = 0;
};

// The four money totals of an order, as required for Data/bill.txt.
struct Totals
{
    double withoutTax = 0.0;
    double tax = 0.0;
    double discount = 0.0;
    double payable = 0.0;  // withoutTax + tax - discount
};

// BillCalculator — pure money maths, nothing else (SRP). No files, no GUI,
// so it is trivially unit-testable and reusable by any front-end.
class BillCalculator
{
public:
    static Totals calculate(const std::vector<CartLine>& lines);
    static double lineTotal(const CartLine& line);  // payable share of one line
    // Round to whole paisa (2 decimals). Applied at every step of every
    // payable computation so the written fields always sum exactly (D-32).
    static double round2(double value);
    // THE payable formula (requirement 8): totals payable after the two
    // bill-level rupee discounts, rounded, never below zero.
    static double payableAfterDiscounts(const Totals& totals,
        double seasonalDiscount, double specialDiscount);
    // v38 (D-10/D-22): the COMBINED seasonal + special discount is
    // capped at 50% of the pre-discount payable, so a 99% season plus
    // an 8% special can never zero a bill. The cap trims the special
    // discount first, then the seasonal, so the two effective amounts
    // always sum to the discount actually taken (D-32: the stored bill
    // fields must sum to the stored payable). Outputs are rounded.
    static void cappedDiscounts(const Totals& totals,
        double seasonalDiscount, double specialDiscount,
        double& seasonalOut, double& specialOut);
};
