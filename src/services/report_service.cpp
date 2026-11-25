#include "services/report_service.h"
#include "common/date_time.h"
#include "common/string_utils.h"

#include <algorithm>
#include <climits>
#include <map>
#include <sstream>

namespace
{
std::string money(double v) { return "PKR " + stringutils::money(v); }

bool sameDay(const std::string& a, const std::string& b) { return a == b; }

// D-04/D-08: real calendar maths - today + previous 6 days = 7 days.
bool withinWeek(const std::string& a, const std::string& today)
{
    const int d = datetime::daysBetween(a, today);
    return d != INT_MIN && d >= 0 && d <= 6;
}

bool withinMonth(const std::string& a, const std::string& today)
{
    if (a.size() != 10 || today.size() != 10) return false;
    return a.substr(0, 7) == today.substr(0, 7);
}

bool withinYear(const std::string& a, const std::string& today)
{
    if (a.size() != 10 || today.size() != 10) return false;
    return a.substr(0, 4) == today.substr(0, 4);
}
}

ReportService::Table ReportService::incompleteOrders()
{
    // D-08: based on ORDERS, not bills - a held order has no bill yet
    // and was invisible in both reports. The amount comes from its bill
    // when one exists, else from the order's own payment amount.
    Table t;
    t.title = "Incomplete Orders";
    t.headers = {"Order Id", "Date", "Customer", "Status", "Payable"};
    const std::vector<Bill> bills = m_bills.loadAll();
    auto billFor = [&](const std::string& orderId) -> const Bill*
    {
        for (const Bill& b : bills)
            if (b.orderId == orderId) return &b;
        return nullptr;
    };
    double total = 0.0;
    for (const Order& o : m_orders.loadAll())
    {
        if (o.status != "on-hold" && o.status != "in-progress") continue;
        const Bill* b = billFor(o.id);
        const double amount = b ? b->totalPayable : o.paymentAmount;
        t.rows.push_back({o.id, o.date, o.customerName,
            o.status, money(amount)});
        total += amount;
    }
    t.summary = "Total (incomplete): " + money(total);
    return t;
}

ReportService::Table ReportService::completedOrders()
{
    // D-08: based on ORDERS, not bills - archiving an order no longer
    // leaves it "incomplete" because its bill still says otherwise.
    Table t;
    t.title = "Completed Orders";
    t.headers = {"Order Id", "Date", "Customer", "Status", "Payable"};
    const std::vector<Bill> bills = m_bills.loadAll();
    auto billFor = [&](const std::string& orderId) -> const Bill*
    {
        for (const Bill& b : bills)
            if (b.orderId == orderId) return &b;
        return nullptr;
    };
    double total = 0.0;
    for (const Order& o : m_orders.loadAll())
    {
        if (o.status != "paid" && o.status != "archived") continue;
        const Bill* b = billFor(o.id);
        const double amount = b ? b->totalPayable : o.paymentAmount;
        t.rows.push_back({o.id, o.date, o.customerName,
            o.status, money(amount)});
        total += amount;
    }
    t.summary = "Total (completed): " + money(total);
    return t;
}

ReportService::Table ReportService::cashSales(const std::string& mode,
    const std::string& fromDate, const std::string& toDate)
{
    Table t;
    t.title = "Cash Sales (" + mode + ")";
    t.headers = {"Bill Id", "Date", "Customer", "Payable", "Received"};
    // D-08: a custom period with From after To is an error, not an
    // empty table with a PKR 0.00 total.
    if (mode == "custom")
    {
        if (!datetime::isValidDate(fromDate) || !datetime::isValidDate(toDate))
        {
            t.summary = "Error: custom dates must be YYYY-MM-DD.";
            return t;
        }
        if (fromDate > toDate)
        {
            t.summary = "Error: 'From' date must be on or before 'To' date.";
            return t;
        }
    }
    const std::string today = datetime::todayDate();
    // "Cash sales" means exactly that: only bills whose order was paid
    // in cash. Bills do not store the payment mode, orders do - join.
    std::vector<std::string> cashOrderIds;
    for (const Order& o : m_orders.loadAll())
        if (o.paymentMode == "cash") cashOrderIds.push_back(o.id);
    double total = 0.0;
    for (const Bill& b : m_bills.loadAll())
    {
        // D-08: only PAID bills are sales.
        if (b.paymentStatus != "paid") continue;
        bool isCash = false;
        for (const std::string& id : cashOrderIds)
            if (id == b.orderId) { isCash = true; break; }
        if (!isCash) continue;
        // D-04/D-08: the bill date IS the payment date (checkout sets
        // the order date to today when a resumed order is paid), so
        // cash sales land in the period they were actually paid in.
        bool inRange = false;
        if (mode == "daily")   inRange = sameDay(b.billDate, today);
        else if (mode == "weekly")  inRange = withinWeek(b.billDate, today);
        else if (mode == "monthly") inRange = withinMonth(b.billDate, today);
        else if (mode == "yearly")  inRange = withinYear(b.billDate, today);
        else if (mode == "custom")  inRange = (b.billDate >= fromDate && b.billDate <= toDate);
        if (!inRange) continue;
        t.rows.push_back({b.id, b.billDate, b.customerName,
            money(b.totalPayable), money(b.amountReceived)});
        total += b.totalPayable;
    }
    t.summary = "Total cash sales: " + money(total);
    return t;
}

ReportService::Table ReportService::allSalesWithDiscounts()
{
    Table t;
    t.title = "All Sales with Discounts";
    t.headers = {"Bill Id", "Date", "Payable", "Product Disc", "Seasonal", "Special"};
    double total = 0.0;
    double totalDiscount = 0.0;
    double productDiscount = 0.0;
    double seasonalDiscount = 0.0;
    double specialDiscount = 0.0;
    for (const Bill& b : m_bills.loadAll())
    {
        // D-08: on-hold / in-progress bills are not sales.
        if (b.paymentStatus != "paid") continue;
        t.rows.push_back({b.id, b.billDate, money(b.totalPayable),
            money(b.totalDiscount), money(b.seasonalDiscount), money(b.specialDiscount)});
        total += b.totalPayable;
        productDiscount += b.totalDiscount;
        seasonalDiscount += b.seasonalDiscount;
        specialDiscount += b.specialDiscount;
    }
    // D-08: the total discount is summed, not left to the reader.
    totalDiscount = productDiscount + seasonalDiscount + specialDiscount;
    t.summary = "Total sales: " + money(total)
        + "; Total discount: " + money(totalDiscount)
        + " (product " + money(productDiscount)
        + ", seasonal " + money(seasonalDiscount)
        + ", special " + money(specialDiscount) + ")";
    return t;
}

ReportService::Table ReportService::topTenSoldItems()
{
    // D-08: aggregate by PRODUCT ID (not acronym) over PAID bills only.
    // BillDetail rows carry the acronym, so the id is resolved through
    // the product list; ties break by acronym so the order is stable.
    std::vector<Product> products = m_products.loadAll();
    auto productForAcronym = [&](const std::string& acronym) -> const Product*
    {
        for (const Product& p : products)
            if (p.acronym == acronym) return &p;
        return nullptr;
    };
    std::map<std::string, const Bill*> paidBillById;
    const std::vector<Bill> bills = m_bills.loadAll();
    for (const Bill& b : bills)
        if (b.paymentStatus == "paid") paidBillById[b.id] = &b;

    struct Agg { int qty = 0; std::string acronym; };
    std::map<int, Agg> byProductId;  // product id -> units sold
    for (const BillDetail& d : m_details.loadAll())
    {
        if (!paidBillById.count(d.billId)) continue;
        const Product* p = productForAcronym(d.productAcronym);
        if (!p) continue;  // acronym no longer in the catalogue
        Agg& agg = byProductId[p->id];
        agg.acronym = p->acronym;
        agg.qty += d.quantity;
    }

    std::vector<std::pair<int, Agg>> flat;
    for (const auto& entry : byProductId) flat.push_back(entry);
    std::sort(flat.begin(), flat.end(),
        [](const auto& a, const auto& b)
        {
            if (a.second.qty != b.second.qty) return a.second.qty > b.second.qty;
            return a.second.acronym < b.second.acronym;
        });

    auto stockOf = [&](int productId) -> int
    {
        for (const Product& p : products)
            if (p.id == productId) return p.availableQuantity;
        return 0;
    };

    Table t;
    t.title = "Top 10 Most Sold Items";
    t.headers = {"Rank", "Acronym", "Units Sold", "Remaining Stock"};
    int rank = 1;
    for (const auto& entry : flat)
    {
        if (rank > 10) break;
        std::ostringstream rankS;
        rankS << rank;
        t.rows.push_back({rankS.str(), entry.second.acronym,
            std::to_string(entry.second.qty),
            std::to_string(stockOf(entry.first))});
        ++rank;
    }
    t.summary = "Top " + std::to_string(t.rows.size()) + " of " + std::to_string(flat.size()) + " items";
    return t;
}

ReportService::Table ReportService::topTenRemainingStock()
{
    // D-08, literal spec reading of "descending": highest stock first.
    // (If the TA reads this report as purchase planning, the doc batch
    // records that this is a deliberate highest-first reading.)
    std::vector<Product> products = m_products.loadAll();
    std::sort(products.begin(), products.end(),
        [](const Product& a, const Product& b)
        { return a.availableQuantity > b.availableQuantity; });

    Table t;
    t.title = "Top 10 Items with Most Remaining Stock";
    t.headers = {"Rank", "Acronym", "Name", "Remaining Stock"};
    int rank = 1;
    for (const Product& p : products)
    {
        if (rank > 10) break;
        std::ostringstream rankS;
        rankS << rank;
        t.rows.push_back({rankS.str(), p.acronym, p.name,
            std::to_string(p.availableQuantity)});
        ++rank;
    }
    t.summary = "Top " + std::to_string(t.rows.size()) + " of " +
        std::to_string(products.size()) + " items";
    return t;
}
