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

// DataHealth - the answer to "is the data consistent?" (D-02). A paid
// order with no bill, a bill with no order, or a BillDetail row with no
// bill can only come from a partial write; the Self-Test screen shows
// these counts instead of silently loading partial state. On-hold /
// in-progress / archived orders have no bill by design and are not counted.
struct DataHealth
{
    int ordersWithoutBill = 0;   // paid orders with no bill row
    int billsWithoutOrder = 0;   // bills whose order id matches no order
    int orphanBillDetails = 0;   // BillDetail rows whose bill id matches no bill
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
    // Change the quantity of cart line `index` by `delta`. Returns "" on
    // success, else "bad-index", "not-found" (product deleted, stock
    // cannot be verified) or "low-stock" (D-15: was void and silently
    // did nothing on over-stock). Dropping to 0 or below removes the line.
    std::string changeQuantity(std::size_t index, int delta);
    void removeLine(std::size_t index);

    const std::vector<CartLine>& lines() const { return m_lines; }
    bool empty() const { return m_lines.empty(); }
    void clear() { m_lines.clear(); m_resumedOrderId.clear(); m_resumeWarning.clear(); }
    Totals totals() const;

    // Record the cart as an order and generate its bill. The special
    // discount is a percentage (0-8). The amount received must be given
    // explicitly and must cover the payable - it is NEVER silently
    // rewritten to full payment, and an underpaid order is NEVER recorded
    // as paid (D-06). Status must be "paid": a bill is never written for
    // an on-hold / in-progress / archived order, and no stock moves for
    // one. Returns "" on success, else "empty-order", "no-customer",
    // "bad-discount", "bad-status", "bad-payment", "low-stock",
    // "underpayment", "already-billed", "id-collision", "bill-error",
    // "stock-failed" or "write-failed".
    std::string checkout(const std::string& customerName,
        const std::string& customerContact, const std::string& status,
        const std::string& paymentMode, const std::string& paymentMerchant,
        double specialDiscountPercent, double amountReceived,
        CompletedBill& out);

    // Save the cart as an on-hold order (no bill, no stock movement -
    // stock moves when the resumed order is checked out). The payment
    // mode/merchant are validated the same way as checkout (D-06/D-22).
    // Returns "" on success, else "empty-order", "no-customer",
    // "bad-payment", "id-collision" or "write-failed".
    std::string holdOrder(const std::string& customerName,
        const std::string& customerContact, const std::string& paymentMode,
        const std::string& paymentMerchant);

    // Load an on-hold (or in-progress) order's lines back into the cart
    // and remember its id: a later checkout completes THAT order in
    // place (same Order Id, its row and detail rows replaced) instead
    // of leaving a ghost on-hold order behind.
    // Returns "" on success, else "not-found" or "wrong-status". Stock
    // is re-checked on resume (D-23): if it has run out since the hold,
    // the lines are still loaded but this returns "low-stock-warning"
    // and resumeWarning() holds "low-stock" - treat it as success with
    // a warning, not as a failure.
    std::string resumeOrder(const std::string& orderId);

    // The warning left by the last resumeOrder() call, "" when the
    // resume was clean. Read it when resumeOrder() returns
    // "low-stock-warning".
    std::string resumeWarning() const { return m_resumeWarning; }

    // Archive an order through the service (D-11): load, set status
    // "archived", save via the repo. Returns "" on success, else
    // "not-found" or "write-failed". The GUI's heldDeleteSelected()
    // routes through this instead of hand-serialising the file.
    std::string archiveOrder(const std::string& orderId);

    // Data consistency counts (D-02) - see DataHealth above.
    DataHealth dataHealth();

    // The gate for editing a paid order (requirement 3): the order
    // must exist, be paid, be within 7 days of its payment date and
    // have a manager's approval. Returns "" and the order when the
    // edit may proceed, else "not-approved", "not-found",
    // "wrong-status" or "too-old".
    std::string editPaidOrder(const std::string& orderId,
        bool managerApproved, CompletedBill& out);

    // The same existence / status / 7-day checks as editPaidOrder but
    // WITHOUT the approval gate: loading an order to look at it is
    // not an edit. Saving still goes through the gate.
    std::string loadPaidOrder(const std::string& orderId, CompletedBill& out);

    // Save an edited paid order: replaces the order's detail rows,
    // rewrites its bill and bill lines in place (same Bill Id),
    // reconciles stock (old quantities back, new quantities out) and
    // updates the order's customer fields and payment amount. The
    // original special discount, amount received and bill date are
    // carried through (D-16) - none is reset. Same return codes as
    // editPaidOrder, plus "empty-order", "no-customer", "low-stock",
    // "bill-error", "stock-failed" and "write-failed".
    std::string saveEditedPaidOrder(const std::string& orderId,
        const std::string& customerName,
        const std::string& customerContact,
        const std::vector<CartLine>& newLines, bool managerApproved,
        CompletedBill& out);

private:
    // D-04: the PAYMENT date of an order - its bill's date - falling
    // back to the order date when no bill exists yet.
    std::string paymentDateFor(const std::string& orderId,
        const std::string& fallbackDate);

    ProductService& m_products;
    SeasonsService& m_seasons;
    IOrderRepository& m_orders;
    IOrderDetailRepository& m_details;
    IBillRepository& m_bills;
    IBillDetailRepository& m_billDetails;
    std::vector<CartLine> m_lines;
    std::string m_resumedOrderId;
    std::string m_resumeWarning;  // set by resumeOrder(), see resumeWarning()
};
