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

    // v37 (D-02): the write-ahead journal entry goes FIRST - before
    // ANY data file is touched and before any stock moves. It carries
    // everything recovery needs (ids, amounts, lines with the stock
    // snapshot) and is appended through the same RecordFile::appendLine
    // path as the other files. A failure here leaves nothing written
    // and the cart kept, so the cashier can retry.
    const JournalSerialiser::Entry journalEntry = buildJournalEntry("checkout",
        order, billId, seasonalAmount, specialAmount, amountReceived, m_lines, {});
    if (!m_journal.append(journalEntry)) return "journal-failed";

    // The pre-existing rows a resumed completion would overwrite, so
    // a write failure can put them back (v37 rollback, see below).
    std::vector<OrderDetail> storedDetails;
    if (completing) storedDetails = m_details.loadForOrder(order.id);

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
    if (!written)
    {
        // v37: the attempt failed AFTER the journal write. Roll its
        // partial rows back and abort the entry, so a retry (which
        // generates NEW ids) cannot be completed twice by a later
        // recovery. The rollback is best-effort; if it fails, the
        // entry stays pending and recovery rolls it forward instead.
        rollbackCheckoutWrites(order.id, billId, completing, stored, storedDetails);
        m_journal.markAborted(order.id, "checkout");
        return "write-failed";
    }

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
        // v37: the entry stays `pending` on purpose - the order and
        // bill ARE written, only the stock is short, so the next
        // startup recovery completes exactly the missing stock moves
        // instead of the sale being lost or duplicated.
        return "stock-failed";
    }
    m_resumedOrderId.clear();
    // v37: the checkout is complete - all writes and all stock moves.
    // Best-effort: if the mark fails, the entry stays pending and the
    // next recovery re-checks everything and marks it done (a no-op).
    m_journal.markDone(order.id, "checkout");

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
    // D-26a (v38): an anonymous hold is allowed and stored as
    // "Walk-in". Checkout still refuses an empty customer name (see
    // checkout() - a bill needs a name), only the hold is relaxed.
    const std::string customer =
        customerName.empty() ? "Walk-in" : customerName;
    std::string merchant = paymentMerchant;
    if (const std::string err = validatePayment(paymentMode, merchant); !err.empty())
        return err;

    // D-23 (v38): re-holding a resumed order updates it IN PLACE -
    // same Order Id, status on-hold, updated customer/lines/payment
    // fields, and this order's detail rows replaced with the current
    // cart lines. A hold moves NO stock, so no stock is touched here.
    // m_resumedOrderId is KEPT set: it is still the same logical
    // order, so a later checkout must still supersede it in place
    // (checkout() only supersedes when m_resumedOrderId is set).
    if (!m_resumedOrderId.empty())
    {
        std::vector<Order> allOrders = m_orders.loadAll();
        for (Order& o : allOrders)
        {
            if (o.id != m_resumedOrderId) continue;
            o.date = IdGenerator::todayDate();
            o.time = IdGenerator::nowTime();
            o.customerName = customer;
            o.customerContact = customerContact;
            o.status = "on-hold";
            o.paymentAmount = totals().payable;
            o.paymentMode = paymentMode;
            o.paymentMerchant = merchant;
            std::vector<OrderDetail> details;
            for (const CartLine& line : m_lines)
                details.push_back({o.id, line.product.id,
                    line.product.barcode, line.quantity});
            std::vector<OrderDetail> allDetails = m_details.loadAll();
            allDetails.erase(std::remove_if(allDetails.begin(), allDetails.end(),
                [&](const OrderDetail& d) { return d.orderId == o.id; }),
                allDetails.end());
            for (const OrderDetail& d : details) allDetails.push_back(d);
            bool written = m_orders.saveAll(allOrders);
            written = m_details.saveAll(allDetails) && written;
            if (!written) return "write-failed";
            m_lines.clear();
            return "";
        }
        // The resumed order is gone (deleted elsewhere): fall through
        // to a fresh hold instead of failing the cashier's hold.
        m_resumedOrderId.clear();
    }

    Order order;
    order.id = uniqueOrderId(m_orders);  // D-01
    if (order.id.empty()) return "id-collision";
    order.date = IdGenerator::todayDate();
    order.time = IdGenerator::nowTime();
    order.customerName = customer;
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

    const Totals t = BillCalculator::calculate(newLines);

    // The bill keeps its original Bill Id (ids cannot repeat in
    // Bill.txt). D-16: it also keeps its ORIGINAL bill date (the payment
    // date), its ORIGINAL special discount and its ORIGINAL amount
    // received - none is reset to today / 0 / payable. The seasonal
    // discount is recomputed for the ORIGINAL bill date, not today.
    // v37: this whole block moved BEFORE any stock move or file write,
    // so the journal below can carry the bill id and the amounts, and a
    // failure here leaves stock and files untouched (the old code had
    // already increased the stock and had to roll it back).
    std::vector<Bill> bills = m_bills.loadAll();
    Bill* billPtr = nullptr;
    for (Bill& b : bills)
        if (b.orderId == orderId) { billPtr = &b; break; }
    if (!billPtr) return "not-found";
    const double seasonalPercent =
        m_seasons.activeDiscountPercentOn(billPtr->billDate);
    const double seasonalAmount =
        BillCalculator::round2(t.payable * seasonalPercent / 100.0);
    const double specialAmount = billPtr->specialDiscount;  // carried through

    Order billOrder = *target;
    billOrder.customerName = customerName;
    billOrder.customerContact = customerContact;
    billOrder.date = billPtr->billDate;
    billOrder.time = billPtr->billTime;
    BillService billService(m_bills, m_billDetails);
    const auto billOpt = billService.buildBill(billOrder, t, billPtr->id,
        seasonalAmount, specialAmount, billPtr->amountReceived);
    if (!billOpt) return "bill-error";  // D-32: the money does not add up
    const Bill bill = *billOpt;

    // D-15: sum quantities per product before checking stock - two new
    // lines for the same product must be checked together. v37: the
    // check is non-mutating now - the new lines must fit in the stock
    // PLUS the old quantities the edit gives back. Same result as the
    // old increase-then-check, with no movement yet.
    std::map<int, int> oldQty;  // product id -> quantity the edit returns
    for (const OrderDetail& d : oldLines) oldQty[d.productId] += d.quantity;
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
        if (!fresh || entry.second.second
                > fresh->availableQuantity + oldQty[entry.first])
            return "low-stock";
    }

    // v37 (D-02): the journal entry goes FIRST - before the stock
    // increase and before any file is rewritten. A failure here leaves
    // everything untouched. On a later write/stock failure the entry
    // stays `pending` on purpose: the edit was validated and approved,
    // so the next startup recovery rolls it forward to the same target
    // state (an edit is deterministic - re-applying it is a no-op).
    const JournalSerialiser::Entry journalEntry = buildJournalEntry("edit",
        billOrder, billPtr->id, seasonalAmount, specialAmount,
        billPtr->amountReceived, newLines, oldLines);
    if (!m_journal.append(journalEntry)) return "journal-failed";

    // Give the old quantities back first, then take the new ones out
    // after the files are rewritten (unchanged from v35).
    for (const OrderDetail& d : oldLines)
        m_products.increaseStock(d.productId, d.quantity);
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
        // v37: the journal entry stays `pending` - recovery rolls this
        // validated, approved edit forward to its target state.
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
        // v37: the entry stays `pending` - the files hold the edit but
        // the stock is short, so recovery sets the stock to the exact
        // expected values instead of the edit being lost.
        return "stock-failed";
    }

    // v37: the edit is complete. Best-effort, as in checkout: a failed
    // mark leaves a pending entry that the next recovery no-ops to done.
    m_journal.markDone(orderId, "edit");

    out.order = *target;
    out.details = newDetails;
    out.bill = bill;
    out.billDetail = billLines;
    out.totals = t;
    out.billId = bill.id;
    return "";
}

// ------------------------------------------------------------------
// v37 (D-02): write-ahead journal + startup recovery.
// ------------------------------------------------------------------

JournalSerialiser::Entry OrderService::buildJournalEntry(const std::string& kind,
    const Order& order, const std::string& billId,
    double seasonalAmount, double specialAmount, double amountReceived,
    const std::vector<CartLine>& lines,
    const std::vector<OrderDetail>& oldLines)
{
    JournalSerialiser::Entry e;
    e.orderId = order.id;
    e.kind = kind;
    e.billId = billId;
    e.customerName = order.customerName;
    e.customerContact = order.customerContact;
    // The bill date/time: for a checkout the order was just stamped
    // today/now; for an edit the caller passes the order with the
    // ORIGINAL bill date/time, so recovery rebuilds the bill for the
    // right day.
    e.date = order.date;
    e.time = order.time;
    e.paymentMode = order.paymentMode;
    e.paymentMerchant = order.paymentMerchant;
    e.seasonalAmount = seasonalAmount;
    e.specialAmount = specialAmount;
    e.amountReceived = amountReceived;
    for (const CartLine& line : lines)
    {
        // The stock snapshot is taken HERE, at journal-write time -
        // before any file is touched and before any stock moves.
        const auto fresh = m_products.findByBarcode(line.product.barcode);
        const int before = fresh ? fresh->availableQuantity
                                 : line.product.availableQuantity;
        e.lines.push_back({line.product.id, line.quantity, before});
    }
    for (const OrderDetail& d : oldLines)
    {
        const auto fresh = m_products.findById(d.productId);
        const int before = fresh ? fresh->availableQuantity : 0;
        e.oldLines.push_back({d.productId, d.quantity, before});
    }
    e.status = "pending";
    return e;
}

void OrderService::rollbackCheckoutWrites(const std::string& orderId,
    const std::string& billId, bool completing, const Order& storedOrder,
    const std::vector<OrderDetail>& storedDetails)
{
    // Best-effort only: every step is attempted and its result is
    // ignored, so a failure here degrades to "leave pending" rather
    // than to a second failure. The bill row is new in both cases.
    std::vector<Order> orders = m_orders.loadAll();
    if (completing)
    {
        for (Order& o : orders)
            if (o.id == orderId) o = storedOrder;
        m_orders.saveAll(orders);
        std::vector<OrderDetail> details = m_details.loadAll();
        details.erase(std::remove_if(details.begin(), details.end(),
            [&](const OrderDetail& d) { return d.orderId == orderId; }),
            details.end());
        for (const OrderDetail& d : storedDetails) details.push_back(d);
        m_details.saveAll(details);
    }
    else
    {
        orders.erase(std::remove_if(orders.begin(), orders.end(),
            [&](const Order& o) { return o.id == orderId; }), orders.end());
        m_orders.saveAll(orders);
        std::vector<OrderDetail> details = m_details.loadAll();
        details.erase(std::remove_if(details.begin(), details.end(),
            [&](const OrderDetail& d) { return d.orderId == orderId; }),
            details.end());
        m_details.saveAll(details);
    }
    std::vector<Bill> bills = m_bills.loadAll();
    bills.erase(std::remove_if(bills.begin(), bills.end(),
        [&](const Bill& b) { return b.id == billId; }), bills.end());
    m_bills.saveAll(bills);
    std::vector<BillDetail> billDetails = m_billDetails.loadAll();
    billDetails.erase(std::remove_if(billDetails.begin(), billDetails.end(),
        [&](const BillDetail& d) { return d.billId == billId; }), billDetails.end());
    m_billDetails.saveAll(billDetails);
}

RecoveryReport OrderService::recoverJournal()
{
    RecoveryReport report;
    const std::vector<JournalSerialiser::Entry> entries = m_journal.loadAll();
    report.droppedLines = m_journal.droppedLines();
    for (const JournalSerialiser::Entry& entry : entries)
    {
        if (entry.status != "pending") continue;
        if (recoverOneEntry(entry))
        {
            ++report.recovered;
            report.recoveredOrderIds.push_back(entry.orderId);
        }
        else
        {
            ++report.skipped;
        }
    }
    m_lastRecovery = report;
    return report;
}

bool OrderService::recoverOneEntry(const JournalSerialiser::Entry& entry)
{
    // Rebuild the cart lines from the journal. A product the entry
    // needs that no longer exists cannot be rebuilt faithfully, so the
    // entry stays pending (and is counted as skipped, not dropped).
    std::vector<CartLine> lines;
    for (const JournalSerialiser::Line& jl : entry.lines)
    {
        const auto product = m_products.findById(jl.productId);
        if (!product) return false;
        lines.push_back({*product, jl.quantity});
    }

    // Expected final stock per product: stockBefore + oldQty - newQty
    // (oldQty is 0 for a checkout). Every product the entry touches
    // must still exist, and no expected stock may go negative - both
    // are checked BEFORE any file is written, so a skipped entry
    // leaves the data exactly as it was.
    std::map<int, int> stockBefore;
    for (const JournalSerialiser::Line& jl : entry.lines)
        stockBefore[jl.productId] = jl.stockBefore;
    for (const JournalSerialiser::Line& jl : entry.oldLines)
        stockBefore[jl.productId] = jl.stockBefore;
    std::map<int, int> newQty, oldQty;
    for (const JournalSerialiser::Line& jl : entry.lines)
        newQty[jl.productId] += jl.quantity;
    for (const JournalSerialiser::Line& jl : entry.oldLines)
    {
        oldQty[jl.productId] += jl.quantity;
        if (!m_products.findById(jl.productId)) return false;
    }
    std::map<int, int> expectedStock;
    for (const auto& kv : stockBefore)
    {
        const int expected = kv.second + oldQty[kv.first] - newQty[kv.first];
        if (expected < 0) return false;
        expectedStock[kv.first] = expected;
    }

    const Totals t = BillCalculator::calculate(lines);
    BillService billService(m_bills, m_billDetails);

    // The TARGET bill, rebuilt from the journal (same lines, same
    // rupee amounts - no recomputation from percentages, so it matches
    // the bill the interrupted attempt was writing exactly).
    Order billOrder;
    billOrder.id = entry.orderId;
    billOrder.customerName = entry.customerName;
    billOrder.date = entry.date;
    billOrder.time = entry.time;
    billOrder.status = "paid";
    billOrder.paymentMode = entry.paymentMode;
    billOrder.paymentMerchant = entry.paymentMerchant;
    const auto built = billService.buildBill(billOrder, t, entry.billId,
        entry.seasonalAmount, entry.specialAmount, entry.amountReceived);
    if (!built) return false;  // D-32: the money does not add up
    const Bill bill = *built;
    const std::vector<BillDetail> billLines = billService.buildDetails(lines, entry.billId);

    // An existing bill row is kept ONLY when it already equals the
    // target. That distinguishes the two crash points a bare "bill
    // exists" check cannot: a checkout whose bill was already written
    // (keep it) from an edit whose bill still holds the OLD totals
    // (rewrite it). A bill id that belongs to a DIFFERENT order is not
    // ours to touch.
    const auto sameBill = [](const Bill& a, const Bill& b)
    {
        return a.id == b.id && a.customerName == b.customerName
            && a.orderId == b.orderId
            && a.totalWithoutTax == b.totalWithoutTax && a.totalTax == b.totalTax
            && a.totalDiscount == b.totalDiscount
            && a.totalPayable == b.totalPayable
            && a.amountReceived == b.amountReceived
            && a.balanceReturned == b.balanceReturned
            && a.paymentStatus == b.paymentStatus
            && a.seasonalDiscount == b.seasonalDiscount
            && a.specialDiscount == b.specialDiscount
            && a.billDate == b.billDate && a.billTime == b.billTime;
    };
    bool haveBill = false;
    bool billMatches = false;
    if (const auto existing = m_bills.findById(entry.billId))
    {
        if (existing->orderId != entry.orderId) return false;
        haveBill = true;
        billMatches = sameBill(*existing, bill);
    }

    // The order row: create it, or bring the existing row (an on-hold
    // row being completed, or a half-written paid row) to the journal's
    // values. paymentAmount always follows the bill.
    Order order;
    order.id = entry.orderId;
    order.date = entry.date;
    order.time = entry.time;
    order.customerName = entry.customerName;
    order.customerContact = entry.customerContact;
    order.status = "paid";
    order.paymentAmount = bill.totalPayable;
    order.paymentMode = entry.paymentMode;
    order.paymentMerchant = entry.paymentMerchant;

    std::vector<OrderDetail> details;
    for (const CartLine& line : lines)
        details.push_back({entry.orderId, line.product.id,
            line.product.barcode, line.quantity});

    std::vector<Order> orders = m_orders.loadAll();
    bool foundOrder = false;
    for (Order& o : orders)
        if (o.id == order.id) { o = order; foundOrder = true; }
    if (foundOrder)
    {
        if (!m_orders.saveAll(orders)) return false;
    }
    else if (!m_orders.append(order))
    {
        return false;
    }

    // Detail rows: replace this order's rows when they differ from the
    // journal's lines (a crash between the detail appends leaves a
    // partial set). Compared as a set - order in the file does not
    // matter - and detail rows carry no prices, so the comparison is
    // exact.
    const auto sameDetails = [](std::vector<OrderDetail> a, std::vector<OrderDetail> b)
    {
        if (a.size() != b.size()) return false;
        const auto cmp = [](const OrderDetail& x, const OrderDetail& y)
        {
            if (x.productId != y.productId) return x.productId < y.productId;
            if (x.productBarcode != y.productBarcode)
                return x.productBarcode < y.productBarcode;
            return x.quantity < y.quantity;
        };
        std::sort(a.begin(), a.end(), cmp);
        std::sort(b.begin(), b.end(), cmp);
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i].orderId != b[i].orderId || a[i].productId != b[i].productId
                || a[i].productBarcode != b[i].productBarcode
                || a[i].quantity != b[i].quantity)
                return false;
        return true;
    };
    std::vector<OrderDetail> allDetails = m_details.loadAll();
    std::vector<OrderDetail> currentDetails;
    for (const OrderDetail& d : allDetails)
        if (d.orderId == entry.orderId) currentDetails.push_back(d);
    if (!sameDetails(currentDetails, details))
    {
        allDetails.erase(std::remove_if(allDetails.begin(), allDetails.end(),
            [&](const OrderDetail& d) { return d.orderId == entry.orderId; }),
            allDetails.end());
        for (const OrderDetail& d : details) allDetails.push_back(d);
        if (!m_details.saveAll(allDetails)) return false;
    }

    // Bill row + lines: append when the bill is missing; rewrite the
    // row (and its lines) when the existing row is not the target yet
    // - an edit whose bill still holds the old totals, or a bill whose
    // row was written but whose lines are partial. When the existing
    // row already equals the target, rewrite its lines only when they
    // differ (a crash between the BillDetail appends leaves a partial
    // set). BillDetail rows carry the catalogue price at sale time;
    // the catalogue cannot change while the app is down (a crash
    // leaves the app closed until this recovery runs), so the rebuilt
    // rows match the originals.
    const auto sameBillDetails = [](std::vector<BillDetail> a, std::vector<BillDetail> b)
    {
        if (a.size() != b.size()) return false;
        const auto cmp = [](const BillDetail& x, const BillDetail& y)
        {
            if (x.productAcronym != y.productAcronym)
                return x.productAcronym < y.productAcronym;
            if (x.unitPrice != y.unitPrice) return x.unitPrice < y.unitPrice;
            if (x.quantity != y.quantity) return x.quantity < y.quantity;
            return x.lineDiscount < y.lineDiscount;
        };
        std::sort(a.begin(), a.end(), cmp);
        std::sort(b.begin(), b.end(), cmp);
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i].billId != b[i].billId || a[i].productAcronym != b[i].productAcronym
                || a[i].unitPrice != b[i].unitPrice || a[i].quantity != b[i].quantity
                || a[i].lineDiscount != b[i].lineDiscount)
                return false;
        return true;
    };
    const auto rewriteBillDetails = [&](const std::vector<BillDetail>& rows)
    {
        std::vector<BillDetail> allBillDetails = m_billDetails.loadAll();
        allBillDetails.erase(std::remove_if(allBillDetails.begin(), allBillDetails.end(),
            [&](const BillDetail& d) { return d.billId == entry.billId; }),
            allBillDetails.end());
        for (const BillDetail& d : rows) allBillDetails.push_back(d);
        return m_billDetails.saveAll(allBillDetails);
    };
    if (!haveBill)
    {
        if (!billService.appendBill(bill, billLines)) return false;
    }
    else if (!billMatches)
    {
        std::vector<Bill> bills = m_bills.loadAll();
        for (Bill& b : bills)
            if (b.id == bill.id) b = bill;
        if (!m_bills.saveAll(bills)) return false;
        if (!rewriteBillDetails(billLines)) return false;
    }
    else
    {
        std::vector<BillDetail> allBillDetails = m_billDetails.loadAll();
        std::vector<BillDetail> currentBillDetails;
        for (const BillDetail& d : allBillDetails)
            if (d.billId == entry.billId) currentBillDetails.push_back(d);
        if (!sameBillDetails(currentBillDetails, billLines)
            && !rewriteBillDetails(billLines))
            return false;
    }

    // Stock, last: set each touched product to its expected final
    // stock. Absolute (not "decrease by qty"), so a crash halfway
    // through the original decreases - some lines moved, some not -
    // still lands on the same numbers, and a second recovery run is a
    // no-op. A failed setStock leaves the entry pending, so the next
    // startup retries the stock fix (the files are already complete).
    bool stockOk = true;
    for (const auto& kv : expectedStock)
    {
        const auto product = m_products.findById(kv.first);
        if (!product) return false;
        if (product->availableQuantity != kv.second
            && !m_products.setStock(kv.first, kv.second))
            stockOk = false;
    }
    if (!stockOk) return false;

    m_journal.markDone(entry.orderId, entry.kind);
    return true;
}
