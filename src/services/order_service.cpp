#include "services/order_service.h"
#include "services/bill_calculator.h"
#include "services/bill_service.h"
#include "services/id_generator.h"
#include "common/date_time.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <map>
#include <thread>

namespace
{
// D-06/D-22: the ONE payment validation, used by checkout() and
// holdOrder(). Cash has no merchant ("-" in the file; the GUI passes ""
// for it, which is normalised here). Card takes a bank, wallet takes a
// wallet provider. Returns "" when valid, else "bad-payment".
// `merchant` is normalised in place ("" -> "-" for cash).
std::string validatePayment(const std::string& mode, std::string& merchant)
{
    if (mode == "cash")
    {
        if (merchant.empty() || merchant == "-") { merchant = "-"; return ""; }
        return "bad-payment";
    }
    if (mode == "card")
    {
        if (merchant == "ABL" || merchant == "MCB" || merchant == "Meezan") return "";
        return "bad-payment";
    }
    if (mode == "wallet")
    {
        if (merchant == "JazzCash" || merchant == "EasyPaisa" || merchant == "Raast")
            return "";
        return "bad-payment";
    }
    return "bad-payment";
}

// D-01: a fresh Order Id that no loaded order uses yet. Ids are
// second-resolution timestamps, so a collision means "same second":
// wait for the next second and try again (at most 5 tries).
std::string uniqueOrderId(IOrderRepository& orders)
{
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        const std::string id = IdGenerator::newOrderId();
        bool used = false;
        for (const Order& o : orders.loadAll())
            if (o.id == id) { used = true; break; }
        if (!used) return id;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return std::string();
}

// D-01: the same for Bill Ids (9 digits, '1' + HHMMSS + DD).
std::string uniqueBillId(IBillRepository& bills)
{
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        const std::string id = IdGenerator::newBillId();
        if (!bills.findById(id)) return id;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return std::string();
}
}

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

std::string OrderService::paymentDateFor(const std::string& orderId,
    const std::string& fallbackDate)
{
    // D-04: the gate runs on the PAYMENT date - the bill's date - not
    // the order's creation/hold date. No bill yet means not paid, so
    // fall back to the order date.
    for (const Bill& b : m_bills.loadAll())
        if (b.orderId == orderId) return b.billDate;
    return fallbackDate;
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

std::string OrderService::changeQuantity(std::size_t index, int delta)
{
    if (index >= m_lines.size()) return "bad-index";
    const int next = m_lines[index].quantity + delta;
    if (next <= 0)
    {
        m_lines.erase(m_lines.begin() + static_cast<long>(index));
        return "";
    }
    if (delta > 0)
    {
        // D-15: was void and silently did nothing on over-stock, and
        // set the quantity unchecked when the product had been deleted.
        const auto fresh = m_products.findByBarcode(m_lines[index].product.barcode);
        if (!fresh) return "not-found";
        if (!ProductService::canSell(*fresh, next)) return "low-stock";
    }
    m_lines[index].quantity = next;
    return "";
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
    // D-06: a bill is only ever written for a PAID order. Checking out
    // with status on-hold / in-progress / archived must not write a
    // bill and must not move stock.
    if (status != "paid") return "bad-status";
    std::string merchant = paymentMerchant;
    if (const std::string err = validatePayment(paymentMode, merchant); !err.empty())
        return err;

    // D-15: sum quantities per product before checking stock - two
    // lines for the same product must be checked together.
    std::map<int, std::pair<std::string, int>> wanted;  // id -> (barcode, qty)
    for (const CartLine& line : m_lines)
    {
        auto& w = wanted[line.product.id];
        w.first = line.product.barcode;
        w.second += line.quantity;
    }
    for (const auto& entry : wanted)
    {
        const auto fresh = m_products.findByBarcode(entry.second.first);
        if (!fresh || !ProductService::canSell(*fresh, entry.second.second))
            return "low-stock";
    }

    // D-31/D-32: the payable comes from BillCalculator, rounded to whole
    // paisa at every step; no local copy of the formula lives here.
    const Totals t = totals();
    const std::string today = IdGenerator::todayDate();
    const double seasonalPercent = m_seasons.activeDiscountPercentOn(today);
    const double seasonalAmount =
        BillCalculator::round2(t.payable * seasonalPercent / 100.0);
    const double specialAmount =
        BillCalculator::round2(t.payable * specialDiscountPercent / 100.0);
    const double payable =
        BillCalculator::payableAfterDiscounts(t, seasonalAmount, specialAmount);
    // D-06: NEVER rewrite amountReceived <= 0 to full payment, and never
    // record an underpaid order as paid. The amount must be explicit and
    // must cover the payable.
    if (amountReceived < payable) return "underpayment";

    // A resumed order is completed in place: it keeps its Order Id and
    // its row and detail rows are replaced, not duplicated (D-23).
    Order stored;
    bool completing = false;
    if (!m_resumedOrderId.empty())
        for (const Order& o : m_orders.loadAll())
            if (o.id == m_resumedOrderId) { stored = o; completing = true; break; }
    if (completing)
    {
        // D-23: refuse to bill an order that already has a bill - the
        // only path that rewrites a bill is the paid-order edit.
        for (const Bill& b : m_bills.loadAll())
            if (b.orderId == stored.id) return "already-billed";
    }

    // D-01: the ids must be unused at generation time.
    const std::string orderId =
        completing ? stored.id : uniqueOrderId(m_orders);
    if (orderId.empty()) return "id-collision";
    const std::string billId = uniqueBillId(m_bills);
    if (billId.empty()) return "id-collision";

    Order order;
    order.id = orderId;
    // D-04: a resumed order is PAID today, so its date is today - the
    // payment date is what the 7-day gate and the cash reports use.
    order.date = today;
    order.time = IdGenerator::nowTime();
    order.customerName = customerName;
    order.customerContact = customerContact;
    order.status = status;
    order.paymentMode = paymentMode;
    order.paymentMerchant = merchant;

    BillService billService(m_bills, m_billDetails);
    const auto billOpt = billService.buildBill(order, t, billId,
        seasonalAmount, specialAmount, amountReceived);
    if (!billOpt) return "bill-error";  // D-32: the money does not add up
    const Bill bill = *billOpt;
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

    // D-02: EVERY stock decrease is checked. On failure the decreases
    // already made are put back, the cart is kept, and the failure is
    // reported instead of ignored. (The bill is already written at this
    // point, so a stock failure here is visible to dataHealth() on the
    // Self-Test screen rather than silent.)
    std::vector<CartLine> decreased;
    bool stockOk = true;
    for (const CartLine& line : m_lines)
    {
        if (m_products.decreaseStock(line.product.id, line.quantity))
            decreased.push_back(line);
        else { stockOk = false; break; }
    }
    if (!stockOk)
    {
        for (const CartLine& line : decreased)
            m_products.increaseStock(line.product.id, line.quantity);
        return "stock-failed";
    }
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
    std::string merchant = paymentMerchant;
    if (const std::string err = validatePayment(paymentMode, merchant); !err.empty())
        return err;
    m_resumedOrderId.clear();  // a fresh hold is a new order, not a completion

    Order order;
    order.id = uniqueOrderId(m_orders);  // D-01
    if (order.id.empty()) return "id-collision";
    order.date = IdGenerator::todayDate();
    order.time = IdGenerator::nowTime();
    order.customerName = customerName;
    order.customerContact = customerContact;
    order.status = "on-hold";
    order.paymentAmount = totals().payable;
    order.paymentMode = paymentMode;
    order.paymentMerchant = merchant;

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
    // D-23: stock may have run out since the order was held. Re-check
    // (summed per product); the lines are still loaded so the cashier
    // can see and fix the cart, but the caller is warned.
    m_resumeWarning.clear();
    std::map<int, std::pair<std::string, int>> wanted;  // id -> (barcode, qty)
    for (const CartLine& line : m_lines)
    {
        auto& w = wanted[line.product.id];
        w.first = line.product.barcode;
        w.second += line.quantity;
    }
    for (const auto& entry : wanted)
    {
        const auto fresh = m_products.findByBarcode(entry.second.first);
        if (!fresh || !ProductService::canSell(*fresh, entry.second.second))
        {
            m_resumeWarning = "low-stock";
            return "low-stock-warning";
        }
    }
    return "";
}

std::string OrderService::archiveOrder(const std::string& orderId)
{
    // D-11: the GUI's heldDeleteSelected() routes through here instead
    // of hand-serialising order.txt.
    std::vector<Order> orders = m_orders.loadAll();
    for (Order& o : orders)
    {
        if (o.id != orderId) continue;
        o.status = "archived";
        return m_orders.saveAll(orders) ? "" : "write-failed";
    }
    return "not-found";
}

DataHealth OrderService::dataHealth()
{
    // D-02: partial-write orphans, counted for the Self-Test screen.
    DataHealth h;
    const std::vector<Order> orders = m_orders.loadAll();
    const std::vector<Bill> bills = m_bills.loadAll();
    const std::vector<BillDetail> details = m_billDetails.loadAll();
    for (const Order& o : orders)
    {
        // On-hold / in-progress / archived orders have no bill by
        // design; only a PAID order without a bill is an orphan.
        if (o.status != "paid") continue;
        bool hasBill = false;
        for (const Bill& b : bills)
            if (b.orderId == o.id) { hasBill = true; break; }
        if (!hasBill) ++h.ordersWithoutBill;
    }
    for (const Bill& b : bills)
    {
        bool hasOrder = false;
        for (const Order& o : orders)
            if (o.id == b.orderId) { hasOrder = true; break; }
        if (!hasOrder) ++h.billsWithoutOrder;
    }
    for (const BillDetail& d : details)
    {
        bool hasBill = false;
        for (const Bill& b : bills)
            if (b.id == d.billId) { hasBill = true; break; }
        if (!hasBill) ++h.orphanBillDetails;
    }
    return h;
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

    // D-04: the gate runs on the PAYMENT date (the bill's date), with
    // real calendar maths - day 7 allowed, day 8 rejected.
    const int days = datetime::daysBetween(
        paymentDateFor(orderId, target.date), datetime::todayDate());
    if (days == INT_MIN || days < 0 || days > 7) return "too-old";

    out.order = target;
    return "";
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
    const int days = datetime::daysBetween(
        paymentDateFor(orderId, target->date), datetime::todayDate());
    if (days == INT_MIN || days < 0 || days > 7) return "too-old";

    const std::vector<OrderDetail> oldLines = m_details.loadForOrder(orderId);

    // Give the old quantities back first, then check the new lines
    // against the refreshed stock (ProductService::canSell, as always).
    for (const OrderDetail& d : oldLines)
        m_products.increaseStock(d.productId, d.quantity);
    // D-15: sum quantities per product before checking stock - two new
    // lines for the same product must be checked together.
    std::map<int, std::pair<std::string, int>> wanted;  // id -> (barcode, qty)
    for (const CartLine& line : newLines)
    {
        auto& w = wanted[line.product.id];
        w.first = line.product.barcode;
        w.second += line.quantity;
    }
    for (const auto& entry : wanted)
    {
        const auto fresh = m_products.findByBarcode(entry.second.first);
        if (!fresh || !ProductService::canSell(*fresh, entry.second.second))
        {
            // Put the old quantities back out before bailing.
            for (const OrderDetail& d : oldLines)
                m_products.decreaseStock(d.productId, d.quantity);
            return "low-stock";
        }
    }

    const Totals t = BillCalculator::calculate(newLines);

    // The bill keeps its original Bill Id (ids cannot repeat in
    // Bill.txt). D-16: it also keeps its ORIGINAL bill date (the payment
    // date), its ORIGINAL special discount and its ORIGINAL amount
    // received - none is reset to today / 0 / payable. The seasonal
    // discount is recomputed for the ORIGINAL bill date, not today.
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
    const double seasonalPercent =
        m_seasons.activeDiscountPercentOn(billPtr->billDate);
    const double seasonalAmount =
        BillCalculator::round2(t.payable * seasonalPercent / 100.0);
    const double specialAmount = billPtr->specialDiscount;  // carried through

    Order billOrder = *target;
    billOrder.customerName = customerName;
    billOrder.date = billPtr->billDate;
    billOrder.time = billPtr->billTime;
    BillService billService(m_bills, m_billDetails);
    const auto billOpt = billService.buildBill(billOrder, t, billPtr->id,
        seasonalAmount, specialAmount, billPtr->amountReceived);
    if (!billOpt)
    {
        for (const OrderDetail& d : oldLines)
            m_products.decreaseStock(d.productId, d.quantity);
        return "bill-error";  // D-32: the money does not add up
    }
    const Bill bill = *billOpt;
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
    if (!written)
    {
        // D-16: roll the stock increase back - the old quantities go
        // out again, so stock is exactly as it was before the edit.
        for (const OrderDetail& d : oldLines)
            m_products.decreaseStock(d.productId, d.quantity);
        return "write-failed";
    }

    // D-02: check every decrease; on failure put back what was taken.
    std::vector<CartLine> decreased;
    bool stockOk = true;
    for (const CartLine& line : newLines)
    {
        if (m_products.decreaseStock(line.product.id, line.quantity))
            decreased.push_back(line);
        else { stockOk = false; break; }
    }
    if (!stockOk)
    {
        for (const CartLine& line : decreased)
            m_products.increaseStock(line.product.id, line.quantity);
        return "stock-failed";
    }

    out.order = *target;
    out.details = newDetails;
    out.bill = bill;
    out.billDetail = billLines;
    out.totals = t;
    out.billId = bill.id;
    return "";
}
