#include "services/order_service.h"
#include "services/id_generator.h"
#include "services/bill_service.h"

std::string OrderService::addLine(const std::string& barcode, int quantity)
{
    if (quantity <= 0) return "bad-quantity";
    const auto product = m_products.findByBarcode(barcode);
    if (!product) return "not-found";

    // Merge with an existing line for the same product, checking the
    // COMBINED quantity against stock, not just this addition.
    for (CartLine& line : m_lines)
    {
        if (line.product.id == product->id)
        {
            if (!ProductService::canSell(*product, line.quantity + quantity))
                return "low-stock";
            line.quantity += quantity;
            line.product = *product;  // refresh price/stock snapshot
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
    // Re-check against live stock before allowing an increase.
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
    CompletedBill& out)
{
    if (m_lines.empty()) return "empty-order";
    if (customerName.empty()) return "no-customer";

    // Final stock re-check against the live file before writing anything.
    for (const CartLine& line : m_lines)
    {
        const auto fresh = m_products.findByBarcode(line.product.barcode);
        if (!fresh || !ProductService::canSell(*fresh, line.quantity))
            return "low-stock";
    }

    const Totals t = totals();
    Order order;
    order.id = IdGenerator::newOrderId();
    order.date = IdGenerator::todayDate();
    order.time = IdGenerator::nowTime();
    order.customerName = customerName;
    order.customerContact = customerContact;
    order.status = status;
    order.paymentAmount = t.payable;
    order.paymentMode = paymentMode;
    order.paymentMerchant = paymentMerchant;

    const std::string billId = IdGenerator::newBillId();
    std::vector<OrderDetail> details;
    for (const CartLine& line : m_lines)
        details.push_back({order.id, line.product.id, line.product.barcode, line.quantity});

    // Building the bill rows is BillService's job (SRP): one row per
    // cart line, all sharing the Bill Id and the order totals.
    BillService billService(m_bills);
    const std::vector<Bill> rows = billService.buildRows(order, m_lines, t, billId);

    // Every write is checked: a bill is only "generated" when the order,
    // ALL its detail rows and ALL its bill rows actually reached disk.
    bool written = m_orders.append(order);
    for (const OrderDetail& d : details) written = m_details.append(d) && written;
    written = billService.appendRows(rows) && written;
    if (!written) return "write-failed";
    for (const CartLine& line : m_lines)
        m_products.decreaseStock(line.product.id, line.quantity);

    out.order = order;
    out.details = details;
    out.billRows = rows;
    out.totals = t;
    out.billId = billId;
    m_lines.clear();
    return "";
}
