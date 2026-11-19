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
};
