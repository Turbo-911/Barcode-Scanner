#pragma once
#include "domain/bill.h"
#include "domain/order.h"
#include "persistence/i_bill_detail_repo.h"
#include "persistence/i_bill_repo.h"
#include "persistence/i_order_repo.h"
#include "services/bill_calculator.h"
#include "services/product_service.h"
#include "services/seasons_service.h"
#include <string>
#include <vector>

// CompletedBill - everything one finished checkout (or one saved
// paid-order edit) produced: the order row, its detail rows, the ONE
// bill row, the bill's per-product lines, the money totals and the
// Bill Id they all share. The GUI uses it to show the bill overlay.
struct CompletedBill
{
    Order order;
    std::vector<OrderDetail> details;
    Bill bill;
    std::vector<BillDetail> billDetail;
    Totals totals;
    std::string billId;
};

// OrderService - the POS order workflow (SRP): the cart being built,
// checkout (order + bill + stock), holding and resuming orders, and
// editing a paid order under the 7-day / manager-approval rule.
// It owns no file formats: every read and write goes through the
// repository interfaces, and the money maths is BillCalculator's.
class OrderService
{
public:
    OrderService(ProductService& products, SeasonsService& seasons,
        IOrderRepository& orders, IOrderDetailRepository& details,
        IBillRepository& bills, IBillDetailRepository& billDetails);

    // Add quantity units of a product (found by barcode) to the cart.
    // Returns "" on success, else "bad-quantity", "not-found" or
    // "low-stock" (the stock rule is ProductService::canSell).
    std::string addLine(const std::string& barcode, int quantity);
    void changeQuantity(std::size_t index, int delta);
    void removeLine(std::size_t index);

    const std::vector<CartLine>& lines() const { return m_lines; }
    bool empty() const { return m_lines.empty(); }
    void clear() { m_lines.clear(); }
    Totals totals() const;

    // Record the cart as an order and generate its bill. The special
    // discount is a percentage (0-8); amountReceived <= 0 means "paid
    // in full". Returns "" on success, else "empty-order",
    // "no-customer", "bad-discount", "low-stock" or "write-failed".
    std::string checkout(const std::string& customerName,
        const std::string& customerContact, const std::string& status,
        const std::string& paymentMode, const std::string& paymentMerchant,
        double specialDiscountPercent, double amountReceived,
        CompletedBill& out);

    // Save the cart as an on-hold order (no bill, no stock movement -
    // stock moves when the resumed order is checked out).
    std::string holdOrder(const std::string& customerName,
        const std::string& customerContact, const std::string& paymentMode,
        const std::string& paymentMerchant);

    // Load an on-hold (or in-progress) order's lines back into the cart.
    // Returns "" on success, else "not-found" or "wrong-status".
    std::string resumeOrder(const std::string& orderId);

    // The gate for editing a paid order (requirement 3): the order
    // must exist, be paid, be within 7 days of its payment date and
    // have a manager's approval. Returns "" and the order when the
    // edit may proceed, else "not-approved", "not-found",
    // "wrong-status" or "too-old".
    std::string editPaidOrder(const std::string& orderId,
        bool managerApproved, CompletedBill& out);

    // Save an edited paid order: replaces the order's detail rows,
    // rewrites its bill and bill lines in place (same Bill Id),
    // reconciles stock (old quantities back, new quantities out) and
    // updates the order's customer fields and payment amount.
    // Same return codes as editPaidOrder, plus "empty-order",
    // "no-customer", "low-stock" and "write-failed".
    std::string saveEditedPaidOrder(const std::string& orderId,
        const std::string& customerName,
        const std::string& customerContact,
        const std::vector<CartLine>& newLines, bool managerApproved,
        CompletedBill& out);

private:
    ProductService& m_products;
    SeasonsService& m_seasons;
    IOrderRepository& m_orders;
    IOrderDetailRepository& m_details;
    IBillRepository& m_bills;
    IBillDetailRepository& m_billDetails;
    std::vector<CartLine> m_lines;
};
