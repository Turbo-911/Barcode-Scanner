#include "services/order_service.h"
#include "services/bill_calculator.h"
#include "services/bill_service.h"
#include "services/id_generator.h"
#include "common/date_time.h"

#include <ctime>
#include <algorithm>

namespace
{
bool daysBetween(const std::string& from, const std::string& to, int& out)
{
    if (!datetime::isValidDate(from) || !datetime::isValidDate(to)) return false;
    const int fy = std::stoi(from.substr(0, 4));
    const int fm = std::stoi(from.substr(5, 2));
    const int fd = std::stoi(from.substr(8, 2));
    const int ty = std::stoi(to.substr(0, 4));
    const int tm = std::stoi(to.substr(5, 2));
    const int td = std::stoi(to.substr(8, 2));
    out = (ty * 372 + tm * 31 + td) - (fy * 372 + fm * 31 + fd);
    return true;
}
}

std::string OrderService::addLine(const std::string& barcode, int quantity)
{
    if (quantity <= 0) return "bad-quantity";
    const auto product = m_products.findByBarcode(barcode);
    if (!product) return "not-found";

    for (CartLine& line : m_lines)
    {
        if (line.product.id == product->id)
        {
            if (!ProductService::canSell(*product, line.quantity + quantity))
                return "low-stock";
            line.quantity += quantity;
            line.product = *product;
            return "";
        }
    }
    if (!ProductService::canSell(*product, quantity)) return "low-stock";
    m_lines.push_back({*product, quantity});
    return "";
}

void OrderService::changeQuantity(std::size_t index, int delta)
{
    if (index >= m_lines.size()) return;
    const int next = m_lines[index].quantity + delta;
    if (next <= 0)
    {
        m_lines.erase(m_lines.begin() + static_cast<long>(index));
        return;
    }
    if (delta > 0)
    {
        const auto fresh = m_products.findByBarcode(m_lines[index].product.barcode);
        if (fresh && !ProductService::canSell(*fresh, next)) return;
    }
    m_lines[index].quantity = next;
}

void OrderService::removeLine(std::size_t index)
{
    if (index < m_lines.size())
        m_lines.erase(m_lines.begin() + static_cast<long>(index));
}

std::string OrderService::checkout(const std::string& customerName,
    const std::string& customerContact, const std::string& status,
    const std::string& paymentMode, const std::string& paymentMerchant,
    double specialDiscountPercent, double amountReceived,
    CompletedBill& out)
{
    if (m_lines.empty()) return "empty-order";
    if (customerName.empty()) return "no-customer";
    if (specialDiscountPercent < 0.0 || specialDiscountPercent > 8.0)
        return "bad-discount";

    for (const CartLine& line : m_lines)
    {
        const auto fresh = m_products.findByBarcode(line.product.barcode);
        if (!fresh || !ProductService::canSell(*fresh, line.quantity))
            return "low-stock";
    }

    const Totals t = totals();
    const std::string today = IdGenerator::todayDate();

    const double seasonalPercent = m_seasons.activeDiscountPercentOn(today);
    const double seasonalAmount = (t.withoutTax + t.tax - t.discount) * seasonalPercent / 100.0;
    const double specialAmount = (t.withoutTax + t.tax - t.discount) * specialDiscountPercent / 100.0;
    if (amountReceived <= 0.0) amountReceived = t.payable - seasonalAmount - specialAmount;
    if (amountReceived < 0.0) amountReceived = 0.0;

    // A resumed order is completed in place: it keeps its Order Id
    // and its original date; only its state moves on.
    Order stored;
    bool completing = false;
    if (!m_resumedOrderId.empty())
        for (const Order& o : m_orders.loadAll())
            if (o.id == m_resumedOrderId) { stored = o; completing = true; break; }

    Order order;
    order.id = completing ? stored.id : IdGenerator::newOrderId();
    order.date = completing ? stored.date : today;
    order.time = IdGenerator::nowTime();
    order.customerName = customerName;
    order.customerContact = customerContact;
    order.status = status;
    order.paymentMode = paymentMode;
    order.paymentMerchant = paymentMerchant;

    const std::string billId = IdGenerator::newBillId();
    BillService billService(m_bills, m_billDetails);
    const Bill bill = billService.buildBill(order, t, billId,
        seasonalAmount, specialAmount, amountReceived);
    order.paymentAmount = bill.totalPayable;

    std::vector<OrderDetail> details;
    for (const CartLine& line : m_lines)
        details.push_back({order.id, line.product.id, line.product.barcode, line.quantity});

    const std::vector<BillDetail> billLines = billService.buildDetails(m_lines, billId);

    bool written;
    if (completing)
    {
        std::vector<Order> allOrders = m_orders.loadAll();
        for (Order& o : allOrders) if (o.id == order.id) o = order;
        written = m_orders.saveAll(allOrders);
        std::vector<OrderDetail> allDetails = m_details.loadAll();
        allDetails.erase(std::remove_if(allDetails.begin(), allDetails.end(),
            [&](const OrderDetail& d) { return d.orderId == order.id; }), allDetails.end());
        for (const OrderDetail& d : details) allDetails.push_back(d);
        written = m_details.saveAll(allDetails) && written;
        written = billService.appendBill(bill, billLines) && written;
    }
    else
    {
        written = m_orders.append(order);
        for (const OrderDetail& d : details) written = m_details.append(d) && written;
        written = billService.appendBill(bill, billLines) && written;
    }
    if (!written) return "write-failed";

    for (const CartLine& line : m_lines)
        m_products.decreaseStock(line.product.id, line.quantity);
    m_resumedOrderId.clear();

    out.order = order;
    out.details = details;
    out.bill = bill;
    out.billDetail = billLines;
    out.totals = t;
    out.billId = billId;
    m_lines.clear();
    return "";
}

std::string OrderService::holdOrder(const std::string& customerName,
    const std::string& customerContact, const std::string& paymentMode,
    const std::string& paymentMerchant)
{
    if (m_lines.empty()) return "empty-order";
    if (customerName.empty()) return "no-customer";
    m_resumedOrderId.clear();  // a fresh hold is a new order, not a completion

    Order order;
    order.id = IdGenerator::newOrderId();
    order.date = IdGenerator::todayDate();
    order.time = IdGenerator::nowTime();
    order.customerName = customerName;
    order.customerContact = customerContact;
    order.status = "on-hold";
    order.paymentAmount = totals().payable;
    order.paymentMode = paymentMode;
    order.paymentMerchant = paymentMerchant;

    std::vector<OrderDetail> details;
    for (const CartLine& line : m_lines)
        details.push_back({order.id, line.product.id, line.product.barcode, line.quantity});

    bool written = m_orders.append(order);
    for (const OrderDetail& d : details) written = m_details.append(d) && written;
    if (!written) return "write-failed";
    m_lines.clear();
    return "";
}

std::string OrderService::resumeOrder(const std::string& orderId)
{
    Order target;
    bool found = false;
    for (const Order& o : m_orders.loadAll())
    {
        if (o.id != orderId) continue;
        target = o;
        found = true;
        break;
    }
    if (!found) return "not-found";
    if (target.status != "on-hold" && target.status != "in-progress")
        return "wrong-status";

    std::vector<OrderDetail> rows = m_details.loadForOrder(orderId);
    if (rows.empty()) return "not-found";

    m_lines.clear();
    for (const OrderDetail& d : rows)
    {
        const auto p = m_products.findByBarcode(d.productBarcode);
        if (!p) continue;
        m_lines.push_back({*p, d.quantity});
    }
    m_resumedOrderId = orderId;
    return "";
}

std::string OrderService::editPaidOrder(const std::string& orderId,
    bool managerApproved, CompletedBill& out)
{
    if (!managerApproved) return "not-approved";
    return loadPaidOrder(orderId, out);
}

std::string OrderService::loadPaidOrder(const std::string& orderId, CompletedBill& out)
{
    Order target;
    bool found = false;
    for (const Order& o : m_orders.loadAll())
    {
        if (o.id != orderId) continue;
        target = o;
        found = true;
        break;
    }
    if (!found) return "not-found";
    if (target.status != "paid") return "wrong-status";

    int days = 0;
    if (!daysBetween(target.date, datetime::todayDate(), days)) return "too-old";
    if (days > 7) return "too-old";

    out.order = target;
    return "";
}

// ---- Added in the Phase 3 repair -------------------------------------
// The constructor, totals() and saveEditedPaidOrder() were missing from
// the delivered files (the class declaration had been lost); they are
// defined here to complete the service.

OrderService::OrderService(ProductService& products, SeasonsService& seasons,
    IOrderRepository& orders, IOrderDetailRepository& details,
    IBillRepository& bills, IBillDetailRepository& billDetails)
    : m_products(products), m_seasons(seasons), m_orders(orders),
      m_details(details), m_bills(bills), m_billDetails(billDetails)
{
}

Totals OrderService::totals() const
{
    return BillCalculator::calculate(m_lines);
}

std::string OrderService::saveEditedPaidOrder(const std::string& orderId,
    const std::string& customerName, const std::string& customerContact,
    const std::vector<CartLine>& newLines, bool managerApproved,
    CompletedBill& out)
{
    if (!managerApproved) return "not-approved";
    if (newLines.empty()) return "empty-order";
    if (customerName.empty()) return "no-customer";

    // Same gate as editPaidOrder: the order must be paid and within
    // 7 days of its payment date.
    std::vector<Order> orders = m_orders.loadAll();
    Order* target = nullptr;
    for (Order& o : orders)
        if (o.id == orderId) { target = &o; break; }
    if (!target) return "not-found";
    if (target->status != "paid") return "wrong-status";
    int days = 0;
    if (!daysBetween(target->date, datetime::todayDate(), days)) return "too-old";
    if (days > 7) return "too-old";

    const std::vector<OrderDetail> oldLines = m_details.loadForOrder(orderId);

    // Give the old quantities back first, then check the new lines
    // against the refreshed stock (ProductService::canSell, as always).
    for (const OrderDetail& d : oldLines)
        m_products.increaseStock(d.productId, d.quantity);
    for (const CartLine& line : newLines)
    {
        const auto fresh = m_products.findByBarcode(line.product.barcode);
        if (!fresh || !ProductService::canSell(*fresh, line.quantity))
        {
            // Put the old quantities back out before bailing.
            for (const OrderDetail& d : oldLines)
                m_products.decreaseStock(d.productId, d.quantity);
            return "low-stock";
        }
    }

    const Totals t = BillCalculator::calculate(newLines);
    const std::string today = IdGenerator::todayDate();
    const double seasonalPercent = m_seasons.activeDiscountPercentOn(today);
    const double seasonalAmount =
        (t.withoutTax + t.tax - t.discount) * seasonalPercent / 100.0;

    // The bill keeps its original Bill Id (ids cannot repeat in
    // Bill.txt); it is re-issued with today's date and the new totals.
    std::vector<Bill> bills = m_bills.loadAll();
    Bill* billPtr = nullptr;
    for (Bill& b : bills)
        if (b.orderId == orderId) { billPtr = &b; break; }
    if (!billPtr)
    {
        for (const OrderDetail& d : oldLines)
            m_products.decreaseStock(d.productId, d.quantity);
        return "not-found";
    }
    Order billOrder = *target;
    billOrder.customerName = customerName;
    billOrder.date = today;
    billOrder.time = IdGenerator::nowTime();
    BillService billService(m_bills, m_billDetails);
    const double payable = t.withoutTax + t.tax - t.discount - seasonalAmount;
    Bill bill = billService.buildBill(billOrder, t, billPtr->id,
        seasonalAmount, 0.0, payable);
    *billPtr = bill;

    target->customerName = customerName;
    target->customerContact = customerContact;
    target->paymentAmount = bill.totalPayable;

    std::vector<OrderDetail> newDetails;
    for (const CartLine& line : newLines)
        newDetails.push_back({orderId, line.product.id,
            line.product.barcode, line.quantity});

    // Rewrite the four files: the order row, this order's detail rows,
    // the bill row and the bill's BillDetail rows.
    bool written = m_orders.saveAll(orders);
    std::vector<OrderDetail> allDetails;
    for (const OrderDetail& d : m_details.loadAll())
        if (d.orderId != orderId) allDetails.push_back(d);
    for (const OrderDetail& d : newDetails) allDetails.push_back(d);
    written = m_details.saveAll(allDetails) && written;
    written = m_bills.saveAll(bills) && written;
    const std::vector<BillDetail> billLines =
        billService.buildDetails(newLines, bill.id);
    std::vector<BillDetail> allBillDetails;
    for (const BillDetail& d : m_billDetails.loadAll())
        if (d.billId != bill.id) allBillDetails.push_back(d);
    for (const BillDetail& d : billLines) allBillDetails.push_back(d);
    written = m_billDetails.saveAll(allBillDetails) && written;
    if (!written) return "write-failed";

    for (const CartLine& line : newLines)
        m_products.decreaseStock(line.product.id, line.quantity);

    out.order = *target;
    out.details = newDetails;
    out.bill = bill;
    out.billDetail = billLines;
    out.totals = t;
    out.billId = bill.id;
    return "";
}
