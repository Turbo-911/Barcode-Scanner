#pragma once
#include "domain/bill.h"
#include "domain/order.h"
#include "persistence/i_bill_detail_repo.h"
#include "persistence/i_bill_repo.h"
#include "persistence/i_order_repo.h"
#include "persistence/journal_repo.h"
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

// RecoveryReport - what the last startup recovery (v37, D-02) did.
// The Self-Test screen shows this next to the orphan counts.
struct RecoveryReport
{
    int recovered = 0;       // pending entries rolled forward to completion
    int droppedLines = 0;    // malformed journal lines ignored (crash
                             // during the journal write itself; the
                             // checkout never started, so nothing to do)
    int skipped = 0;         // pending entries left pending (a product
                             // the entry needs no longer exists, or the
                             // rebuilt bill's money does not add up)
    std::vector<std::string> recoveredOrderIds;  // in journal order
};
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
    // one. v37 (D-02): a write-ahead journal entry is appended BEFORE
    // any data file is touched; a crash after that point is rolled
    // forward by recoverJournal() at the next startup. Returns "" on
    // success, else "empty-order", "no-customer", "bad-discount",
    // "bad-status", "bad-payment", "low-stock", "underpayment",
    // "already-billed", "id-collision", "bill-error", "journal-failed"
    // (the journal append itself failed - nothing was written),
    // "stock-failed" or "write-failed".
    std::string checkout(const std::string& customerName,
        const std::string& customerContact, const std::string& status,
        const std::string& paymentMode, const std::string& paymentMerchant,
        double specialDiscountPercent, double amountReceived,
        CompletedBill& out);

    // Save the cart as an on-hold order (no bill, no stock movement -
    // stock moves when the resumed order is checked out). The payment
    // mode/merchant are validated the same way as checkout (D-06/D-22).
    // v38 (D-26a): an empty customer name is stored as "Walk-in".
    // v38 (D-23): re-holding a RESUMED order updates that order IN
    // PLACE (same Order Id, its detail rows replaced with the current
    // cart) and KEEPS m_resumedOrderId set, so a later checkout still
    // supersedes it in place instead of leaving a ghost on-hold row.
    // Returns "" on success, else "empty-order", "bad-payment",
    // "id-collision" or "write-failed". ("no-customer" is gone: an
    // anonymous hold is a Walk-in hold. Checkout still returns
    // "no-customer" for an empty name - a bill needs a name.)
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

    // Startup recovery (v37, D-02): roll forward every `pending`
    // journal entry - re-create any missing piece (order row, detail
    // rows, bill + BillDetail rows) and re-apply any stock decrease
    // that did not happen - then mark the entry `done`. Idempotent:
    // every step first checks the current records/stock, so running it
    // twice changes nothing the second time. A malformed journal line
    // is ignored and counted in the report's droppedLines (the
    // checkout never started, so there is nothing to recover). Entries
    // that cannot be completed (a product is gone, or the rebuilt
    // bill's money does not add up) are left `pending` and counted as
    // skipped, so the next startup tries again. Call once at startup,
    // before the GUI opens. Returns what was done; lastRecovery()
    // keeps the same report for the Self-Test screen.
    RecoveryReport recoverJournal();
    RecoveryReport lastRecovery() const { return m_lastRecovery; }

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
    // carried through (D-16) - none is reset. v37 (D-02): a journal
    // entry is appended BEFORE any stock move or file write, and the
    // stock check is non-mutating (new lines must fit in stock plus
    // the old quantities). Same return codes as editPaidOrder, plus
    // "empty-order", "no-customer", "low-stock", "bill-error",
    // "journal-failed", "stock-failed" and "write-failed".
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

    // v37 (D-02): the journal entry for one checkout/edit attempt,
    // built from the already-validated attempt (ids, bill, amounts).
    // `stockBefore` is snapshotted from the live catalogue here, at
    // journal-write time - before any file is touched and before any
    // stock moves.
    JournalSerialiser::Entry buildJournalEntry(const std::string& kind,
        const Order& order, const std::string& billId,
        double seasonalAmount, double specialAmount, double amountReceived,
        const std::vector<CartLine>& lines,
        const std::vector<OrderDetail>& oldLines);
    // v37 (D-02): best-effort removal of the rows ONE failed checkout
    // attempt appended (fresh attempt: the order/details/bill rows go
    // away; completing a resumed order: the on-hold row and its old
    // detail rows are restored, the new bill row goes away). Used only
    // on the write-failed path, before the journal entry is aborted.
    void rollbackCheckoutWrites(const std::string& orderId,
        const std::string& billId, bool completing, const Order& storedOrder,
        const std::vector<OrderDetail>& storedDetails);
    // v37 (D-02): roll ONE pending entry forward. Returns true when
    // the entry is complete (and now marked done), false when it must
    // stay pending.
    bool recoverOneEntry(const JournalSerialiser::Entry& entry);

    ProductService& m_products;
    SeasonsService& m_seasons;
    IOrderRepository& m_orders;
    IOrderDetailRepository& m_details;
    IBillRepository& m_bills;
    IBillDetailRepository& m_billDetails;
    JournalFileRepository m_journal;  // v37: Data/journal.txt
    RecoveryReport m_lastRecovery;    // v37: result of recoverJournal()
    std::vector<CartLine> m_lines;
    std::string m_resumedOrderId;
    std::string m_resumeWarning;  // set by resumeOrder(), see resumeWarning()
};
