#pragma once
#include "domain/bill.h"
#include "domain/order.h"
#include "domain/order.h"
#include "persistence/i_order_repo.h"
#include "persistence/i_bill_repo.h"
#include "services/bill_calculator.h"
#include "services/product_service.h"
#include <string>
#include <vector>

// The finished bill as shown on screen after checkout.
struct CompletedBill
{
    Order order;
    std::vector<OrderDetail> details;
    std::vector<Bill> billRows;   // one per detail, sharing the Bill Id
    Totals totals;
    std::string billId;
};

// OrderService — recording an order and generating its bill (SRP).
// It orchestrates ProductService (stock), BillCalculator (money) and the
// three repositories (persistence), and knows nothing about files or
// SFML. Any front-end (this GUI, a console, a test) can reuse it.
class OrderService
{
public:
    OrderService(ProductService& products, IOrderRepository& orders,
        IOrderDetailRepository& details, IBillRepository& bills)
        : m_products(products), m_orders(orders), m_details(details), m_bills(bills) {}

    // Adds (or merges) a line. Returns "" on success, else the reason:
    // "not-found", "bad-quantity" or "low-stock".
    std::string addLine(const std::string& barcode, int quantity);
    void changeQuantity(std::size_t index, int delta);
    void removeLine(std::size_t index);
    void clear() { m_lines.clear(); }
    const std::vector<CartLine>& lines() const { return m_lines; }
    Totals totals() const { return BillCalculator::calculate(m_lines); }
    bool empty() const { return m_lines.empty(); }

    // Generates the bill: writes Data/order.txt, Data/orderDetail.txt and Data/bill.txt,
    // and reduces stock in Data/product.txt. Returns "" on success.
    std::string checkout(const std::string& customerName,
        const std::string& customerContact, const std::string& status,
        const std::string& paymentMode, const std::string& paymentMerchant,
        CompletedBill& out);

private:
    ProductService& m_products;
    IOrderRepository& m_orders;
    IOrderDetailRepository& m_details;
    IBillRepository& m_bills;
    std::vector<CartLine> m_lines;
};
