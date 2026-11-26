#pragma once
// stock_audit.h - the "implied opening" stock audit (v37, D-33).
//
// `product.txt` quantity is the REMAINING stock after all recorded
// sales (see DESIGN.md), but no opening stock is recorded anywhere,
// so the audit report could not tell whether a sale or a stock figure
// was wrong. This header makes the reconciliation provable from the
// files alone, with no file-format change:
//
//   implied opening = remaining stock + total sold
//
// Total sold is summed from `orderDetail.txt` by product id. The
// implied opening assumes no unlogged restock, and it counts EVERY
// orderDetail row as a sale - including on-hold / in-progress orders,
// whose stock never moved. For products with held orders the implied
// opening is therefore an upper bound (SUGAR: 52 all-rows vs 42
// paid-only; OIL is unaffected - both its orders are paid).
//
// A product is inconsistent exactly when sold > implied opening,
// which is the same as its remaining stock being negative: under the
// files' own semantics that cannot happen, so such a row WOULD be a
// real inconsistency. The shipped data flags nothing.
//
// Pure functions over already-loaded vectors (SRP): no file IO, no
// service, no GUI. Used by the Inventory screen (the "Opening
// (implied)" column), the Self-Test data-health area, and
// pos_selftest. Header-only so the GUI and the headless test share
// the one computation.
#include "domain/order.h"
#include "domain/product.h"

#include <map>
#include <string>
#include <vector>

namespace stock_audit
{
// Total units sold per product id, summed over every orderDetail row.
inline std::map<int, int> soldByProduct(const std::vector<OrderDetail>& details)
{
    std::map<int, int> sold;
    for (const OrderDetail& d : details)
        sold[d.productId] += d.quantity;
    return sold;
}

// Implied opening stock for one product: remaining + sold.
inline int impliedOpening(const Product& product, int sold)
{
    return product.availableQuantity + sold;
}

struct StockViolation
{
    int productId = 0;
    std::string acronym;
    int sold = 0;
    int opening = 0;    // remaining + sold
    int remaining = 0;  // product.txt quantity (negative here)
};

// Every product whose total sold exceeds its implied opening.
// Empty on the shipped data.
inline std::vector<StockViolation> findViolations(
    const std::vector<Product>& products,
    const std::vector<OrderDetail>& details)
{
    const std::map<int, int> sold = soldByProduct(details);
    std::vector<StockViolation> out;
    for (const Product& p : products)
    {
        const auto it = sold.find(p.id);
        const int s = it == sold.end() ? 0 : it->second;
        const int opening = impliedOpening(p, s);
        if (s > opening)
            out.push_back(StockViolation{p.id, p.acronym, s, opening, p.availableQuantity});
    }
    return out;
}
}  // namespace stock_audit
