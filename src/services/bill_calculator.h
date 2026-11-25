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
};
