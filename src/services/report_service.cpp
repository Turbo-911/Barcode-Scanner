#include "services/report_service.h"
#include "common/date_time.h"
#include "common/string_utils.h"

#include <algorithm>
#include <map>
#include <sstream>

namespace
{
std::string money(double v) { return "PKR " + stringutils::money(v); }

bool sameDay(const std::string& a, const std::string& b) { return a == b; }

bool withinWeek(const std::string& a, const std::string& today)
{
    if (a.size() != 10 || today.size() != 10) return false;
    const int ay = std::stoi(a.substr(0, 4));
    const int am = std::stoi(a.substr(5, 2));
    const int ad = std::stoi(a.substr(8, 2));
    const int ty = std::stoi(today.substr(0, 4));
    const int tm = std::stoi(today.substr(5, 2));
    const int td = std::stoi(today.substr(8, 2));
    const int aN = ay * 372 + am * 31 + ad;
    const int tN = ty * 372 + tm * 31 + td;
    return aN >= tN - 6 && aN <= tN;  // today + previous 6 days = 7
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
    Table t;
    t.title = "Incomplete Orders";
    t.headers = {"Order Id", "Date", "Customer", "Status", "Payable"};
    double total = 0.0;
    for (const Bill& b : m_bills.loadAll())
    {
        if (b.paymentStatus == "paid" || b.paymentStatus == "archived") continue;
        t.rows.push_back({b.orderId, b.billDate, b.customerName,
            b.paymentStatus, money(b.totalPayable)});
        total += b.totalPayable;
    }
    t.summary = "Total (incomplete): " + money(total);
    return t;
}

ReportService::Table ReportService::completedOrders()
{
    Table t;
    t.title = "Completed Orders";
    t.headers = {"Order Id", "Date", "Customer", "Status", "Payable"};
    double total = 0.0;
    for (const Bill& b : m_bills.loadAll())
    {
        if (b.paymentStatus != "paid" && b.paymentStatus != "archived") continue;
        t.rows.push_back({b.orderId, b.billDate, b.customerName,
            b.paymentStatus, money(b.totalPayable)});
        total += b.totalPayable;
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
    const std::string today = datetime::todayDate();
    // "Cash sales" means exactly that: only bills whose order was paid
    // in cash. Bills do not store the payment mode, orders do - join.
    std::vector<std::string> cashOrderIds;
    for (const Order& o : m_orders.loadAll())
        if (o.paymentMode == "cash") cashOrderIds.push_back(o.id);
    double total = 0.0;
    for (const Bill& b : m_bills.loadAll())
    {
        bool isCash = false;
        for (const std::string& id : cashOrderIds)
            if (id == b.orderId) { isCash = true; break; }
        if (!isCash) continue;
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
    for (const Bill& b : m_bills.loadAll())
    {
        t.rows.push_back({b.id, b.billDate, money(b.totalPayable),
            money(b.totalDiscount), money(b.seasonalDiscount), money(b.specialDiscount)});
        total += b.totalPayable;
    }
    t.summary = "Total sales: " + money(total);
    return t;
}

ReportService::Table ReportService::topTenSoldItems()
{
    struct Agg { int qty = 0; };
    std::map<std::string, Agg> byAcronym;
    for (const BillDetail& d : m_details.loadAll())
        byAcronym[d.productAcronym].qty += d.quantity;

    std::vector<std::pair<std::string, int>> flat;
    for (const auto& p : byAcronym) flat.push_back({p.first, p.second.qty});
    std::sort(flat.begin(), flat.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    std::vector<Product> products = m_products.loadAll();
    auto stockOf = [&](const std::string& acronym) -> int
    {
        for (const Product& p : products)
            if (p.acronym == acronym) return p.availableQuantity;
        return 0;
    };

    Table t;
    t.title = "Top 10 Most Sold Items";
    t.headers = {"Rank", "Acronym", "Units Sold", "Remaining Stock"};
    int rank = 1;
    for (const auto& p : flat)
    {
        if (rank > 10) break;
        std::ostringstream rankS;
        rankS << rank;
        t.rows.push_back({rankS.str(), p.first, std::to_string(p.second),
            std::to_string(stockOf(p.first))});
        ++rank;
    }
    t.summary = "Top " + std::to_string(t.rows.size()) + " of " + std::to_string(flat.size()) + " items";
    return t;
}

ReportService::Table ReportService::topTenRemainingStock()
{
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