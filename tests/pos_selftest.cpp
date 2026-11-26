// pos_selftest.cpp - headless test for the POS modules (Phases 2 + 3).
// Run it from an EMPTY folder: it seeds its own Data files there, runs
// checkout / hold / resume / paid-order-edit cycles through the real
// services and repositories, and checks the Data/*.txt files produced.
// It REFUSES to run when Data/product.txt in the current directory
// already holds more than the 2 seeded products (D-13) - running it
// from the repo root would overwrite the real catalogue.
// Build + run in a fresh temp dir (from the project root):
//   mingw32-make -f Makefile.gui test
// Manual build (from the project root):
//   g++ -std=c++17 -Isrc tests/pos_selftest.cpp src/persistence/*.cpp src/services/*.cpp src/domain/*.cpp src/common/string_utils.cpp src/common/date_time.cpp -o pos_selftest
#include "persistence/audit_log.h"
#include "persistence/i_order_repo.h"
#include "persistence/i_bill_repo.h"
#include "persistence/i_bill_detail_repo.h"
#include "persistence/i_category_repo.h"
#include "persistence/i_seasons_repo.h"
#include "persistence/i_product_repo.h"
#include "persistence/journal_repo.h"
#include "persistence/journal_serialiser.h"
#include "persistence/bill_serialiser.h"
#include "persistence/bill_detail_serialiser.h"
#include "persistence/order_detail_serialiser.h"
#include "persistence/order_serialiser.h"
#include "persistence/record_file.h"
#include "services/bill_service.h"
#include "services/category_service.h"
#include "services/id_generator.h"
#include "services/order_service.h"
#include "services/stock_audit.h"
#include "services/pdf_bill.h"
#include "services/report_service.h"
#include "services/seasons_service.h"
#include "common/string_utils.h"
#include "common/date_time.h"
#include "common/pin_hash.h"
#include "persistence/product_serialiser.h"

#include <chrono>
#include <climits>
#include <cmath>
#include <filesystem>
#include <thread>
#include <fstream>
#include <iostream>

namespace
{
int failures = 0;
void check(bool ok, const std::string& label)
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << label << "\n";
    if (!ok) ++failures;
}
bool near(double a, double b) { return std::fabs(a - b) < 0.001; }
}

int main()
{
    // D-13 CWD guard: refuse to run when Data/product.txt in the
    // current directory already holds more than the 2 seeded
    // products - the seeding below truncates that file, so running
    // from the repo root would shrink the real 24-product catalogue
    // to 2 products with no backup. Run from an empty folder (the
    // Makefile.gui `test` target does this for you).
    {
        std::ifstream existing("Data/product.txt");
        if (existing)
        {
            std::size_t lines = 0;
            std::string line;
            while (std::getline(existing, line))
                if (!line.empty()) ++lines;
            if (lines > 2)
            {
                std::cout << "REFUSING TO RUN: Data/product.txt in the current directory already has "
                          << lines << " products.\n"
                          << "pos_selftest seeds its own Data files and would OVERWRITE them.\n"
                          << "Run it from an empty folder instead (mingw32-make -f Makefile.gui test).\n";
                return 2;
            }
        }
    }

    {
        std::filesystem::create_directories("Data");
        std::ofstream cats("Data/PCategory.txt");
        cats << "11 Dairy MLK #\n";
        cats << "12 Bakery BAK #\n";
        std::ofstream seed("Data/product.txt");
        seed << "1001 111001 MILK500 Fresh_Milk count 180.00 10.00 0.00 40 barcodes/barcode_111001.bmp 11 #\n";
        seed << "1002 129876 BREAD Branded_Bread count 120.00 5.00 10.00 25 barcodes/barcode_129876.bmp 12 #\n";
        // One always-on season at 10% so the seasonal path is exercised.
        std::ofstream seasons("Data/Seasons.txt");
        seasons << "2020-01-01 2030-12-31 10.00 true #\n";
    }

    ProductFileRepository productRepo;
    OrderFileRepository orderRepo;
    OrderDetailFileRepository detailRepo;
    BillFileRepository billRepo;
    BillDetailFileRepository billDetailRepo;
    CategoryFileRepository categoryRepo;
    SeasonsFileRepository seasonsRepo;
    CategoryService categories(categoryRepo);
    SeasonsService seasonsService(seasonsRepo);
    ProductService products(productRepo, categories);
    OrderService orders(products, seasonsService, orderRepo, detailRepo,
        billRepo, billDetailRepo);
    ReportService reports(orderRepo, billRepo, billDetailRepo, productRepo);

    const auto milk = products.findByBarcode("111001");
    check(milk && milk->name == "Fresh Milk" && near(milk->unitPrice, 180.0),
        "product lookup by barcode, name '_' decoded to space");
    check(milk && milk->categoryId == "11", "product carries its Category Id (Phase 3)");
    check(!products.findByBarcode("654321"), "unknown barcode not found");
    check(CategoryService::makeBarcode("11", 1001) == "111001",
        "barcode scheme: first two digits are the category");
    check(categories.findById("12") && categories.findById("12")->name == "Bakery",
        "category lookup by id");
    check(stringutils::isValidPhone("03001234567"), "phone rule: 11 digits accepted");
    check(!stringutils::isValidPhone("0300123456"), "phone rule: 10 digits rejected");
    check(!stringutils::isValidPhone("0300-123456"), "phone rule: dashes rejected");
    check(ProductService::canSell(*milk, 39) && !ProductService::canSell(*milk, 40)
        && !ProductService::canSell(*milk, 41)
        && !ProductService::canSell(*milk, 0), "stock rule: qty must be 1..available-1 (v38 D-08: strictly less than available, buying the last unit is refused)");
    check(near(seasonsService.activeDiscountPercentOn(IdGenerator::todayDate()), 10.0),
        "active season gives 10% today");

    check(orders.addLine("111001", 2).empty(), "add 2x milk");
    check(orders.addLine("129876", 1).empty(), "add 1x bread");
    check(orders.addLine("111001", 39) == "low-stock", "merged qty 41 > stock 40 rejected");
    check(orders.addLine("654321", 1) == "not-found", "unknown product rejected");

    const Totals t = orders.totals();
    check(near(t.withoutTax, 480.0) && near(t.tax, 25.0) && near(t.discount, 10.0)
        && near(t.payable, 495.0), "totals: 480 + 25 tax - 10 discount = 495 payable");

    // Seasonal 10% applies to (480 + 25 - 10) = 495 -> 49.50 off, so the
    // customer owes 445.50; paying 600 returns 154.50 balance.
    CompletedBill bill;
    check(orders.checkout("Test Customer", "", "paid", "cash", "", 0.0, 600.0, bill).empty(),
        "checkout succeeds");
    check(bill.order.id.size() == 8 && bill.billId.size() == 9, "order id 8 digits, bill id 9 digits");
    check(bill.order.id.substr(6) == IdGenerator::todayDate().substr(8),
        "order id ends with today's day (date of order)");
    check(near(bill.bill.seasonalDiscount, 49.5) && near(bill.bill.totalPayable, 445.5)
        && near(bill.bill.amountReceived, 600.0) && near(bill.bill.balanceReturned, 154.5),
        "bill: seasonal 49.50 applied, payable 445.50, balance 154.50");

    const auto orderLines = RecordFile::readAllLines("Data/order.txt");
    check(orderLines.size() == 1, "Data/order.txt has exactly 1 line");
    const auto parsedOrder = OrderSerialiser::parse(orderLines.at(0));
    check(parsedOrder && parsedOrder->customerName == "Test Customer"
        && near(parsedOrder->paymentAmount, 445.5) && parsedOrder->status == "paid",
        "Data/order.txt round-trips (name, amount, status)");

    const auto detailLines = RecordFile::readAllLines("Data/orderDetail.txt");
    check(detailLines.size() == 2, "Data/orderDetail.txt has 2 lines");
    const auto d0 = OrderDetailSerialiser::parse(detailLines.at(0));
    check(d0 && d0->orderId == bill.order.id && d0->quantity == 2,
        "OrderDetail row 1 matches the order and quantity");

    const auto billLines = RecordFile::readAllLines("Data/bill.txt");
    check(billLines.size() == 1, "Data/bill.txt has ONE line per bill (Phase 3 format)");
    const auto b0 = BillSerialiser::parse(billLines.at(0));
    check(b0 && b0->id == bill.billId && near(b0->totalWithoutTax, 480.0)
        && near(b0->totalTax, 25.0) && near(b0->totalDiscount, 10.0)
        && near(b0->totalPayable, 445.5) && b0->paymentStatus == "paid",
        "Data/bill.txt row carries id, totals and payment status");

    const auto billDetailLines = RecordFile::readAllLines("Data/BillDetail.txt");
    check(billDetailLines.size() == 2, "Data/BillDetail.txt has one line per product (2)");
    const auto bd1 = BillDetailSerialiser::parse(billDetailLines.at(1));
    check(bd1 && bd1->productAcronym == "BREAD" && near(bd1->lineDiscount, 10.0),
        "BillDetail line discount is per-unit x qty (10 x 1)");

    const auto after = products.findByBarcode("111001");
    check(after && after->availableQuantity == 38, "stock decremented 40 -> 38 after sale");

    // Special discount is capped at 8%: 10% must be refused.
    check(orders.addLine("111001", 1).empty(), "add 1x milk for cap test");
    CompletedBill rejected;
    check(orders.checkout("Cap Test", "", "paid", "cash", "", 10.0, 0.0, rejected)
        == "bad-discount", "special discount above 8% rejected");
    orders.clear();

    // Hold: writes an on-hold order, NO bill, NO stock movement.
    // Order ids are time-based (HHMMSS+day), so wait a second to keep
    // this order's id distinct from the checkout above.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    check(orders.addLine("111001", 1).empty(), "add 1x milk for hold test");
    check(orders.holdOrder("Hold Customer", "", "cash", "").empty(), "hold succeeds");
    check(RecordFile::readAllLines("Data/order.txt").size() == 2, "held order appended");
    check(RecordFile::readAllLines("Data/bill.txt").size() == 1, "hold writes no bill");
    const auto still = products.findByBarcode("111001");
    check(still && still->availableQuantity == 38, "hold does not move stock");
    std::string heldId;
    for (const std::string& line : RecordFile::readAllLines("Data/order.txt"))
        if (const auto o = OrderSerialiser::parse(line); o && o->status == "on-hold")
            heldId = o->id;
    check(!heldId.empty(), "held order is on-hold");
    check(orders.resumeOrder(heldId).empty() && orders.lines().size() == 1,
        "resume reloads the held order's lines");
    check(orders.resumeOrder(bill.order.id) == "wrong-status",
        "a paid order cannot be resumed like a held one");
    orders.clear();

    // Paid-order edit: gate first, then a real save.
    CompletedBill gate;
    check(orders.editPaidOrder(bill.order.id, false, gate) == "not-approved",
        "paid edit blocked without manager approval");
    check(orders.editPaidOrder(bill.order.id, true, gate).empty(),
        "paid edit allowed with approval within 7 days");
    std::vector<CartLine> edited;
    edited.push_back({*products.findByBarcode("111001"), 1});
    CompletedBill saved;
    check(orders.saveEditedPaidOrder(bill.order.id, "Edited Customer", "",
        edited, true, saved).empty(), "paid edit saves");
    check(saved.bill.id == bill.billId && near(saved.bill.totalPayable, 171.0),
        "edited bill keeps its Bill Id; payable 190 - 19 seasonal = 171");
    check(RecordFile::readAllLines("Data/bill.txt").size() == 1,
        "edit rewrites the bill in place (still 1 bill line)");
    check(RecordFile::readAllLines("Data/BillDetail.txt").size() == 1,
        "edit rewrites BillDetail rows (now 1 line)");
    const auto milkNow = products.findByBarcode("111001");
    const auto breadNow = products.findByBarcode("129876");
    check(milkNow && milkNow->availableQuantity == 39 && breadNow
        && breadNow->availableQuantity == 25,
        "stock reconciled by edit (milk 38+2-1=39, bread 24+1=25)");

    // Reports + PDF.
    const ReportService::Table allSales = reports.allSalesWithDiscounts();
    check(allSales.rows.size() == 1, "report: all sales lists the one bill");
    const ReportService::Table cashToday = reports.cashSales("custom",
        IdGenerator::todayDate(), IdGenerator::todayDate());
    check(cashToday.rows.size() == 1, "report: cash sales finds today's cash bill");
    const std::string pdfPath = pdfbill::writePdf("bills_out", saved.bill, saved.billDetail);
    std::ifstream pdf(pdfPath, std::ios::binary);
    std::string magic; std::getline(pdf, magic);
    check(!pdfPath.empty() && magic.rfind("%PDF", 0) == 0, "bill PDF written (%PDF header)");

    {   // Resume + checkout completes the held order IN PLACE: same
        // Order Id, no ghost on-hold row left behind.
        std::string doneId;
        for (const std::string& line : RecordFile::readAllLines("Data/order.txt"))
            if (const auto o = OrderSerialiser::parse(line); o && o->status == "on-hold")
                doneId = o->id;
        check(!doneId.empty(), "an on-hold order is waiting to be completed");
        orders.clear();
        check(orders.resumeOrder(doneId).empty(), "resume the held order for completion");
        const std::size_t orderRows = RecordFile::readAllLines("Data/order.txt").size();
        const std::size_t billRows = RecordFile::readAllLines("Data/bill.txt").size();
        const auto milkBefore = products.findByBarcode("111001");
        CompletedBill rb;
        check(orders.checkout("Hold Customer", "", "paid", "cash", "-", 0.0, 171.0, rb).empty(),
            "checkout after resume succeeds");
        check(rb.order.id == doneId, "resumed checkout keeps the same Order Id");
        check(rb.bill.orderId == doneId, "the bill points at the resumed order");
        check(RecordFile::readAllLines("Data/order.txt").size() == orderRows,
            "no ghost order row is added");
        check(RecordFile::readAllLines("Data/bill.txt").size() == billRows + 1,
            "exactly one new bill is written");
        bool paidNow = false;
        for (const std::string& line : RecordFile::readAllLines("Data/order.txt"))
            if (const auto o = OrderSerialiser::parse(line);
                o && o->id == doneId && o->status == "paid")
                paidNow = true;
        check(paidNow, "the held order's row is now paid");
        const auto milkAfter = products.findByBarcode("111001");
        check(milkBefore && milkAfter
                && milkAfter->availableQuantity == milkBefore->availableQuantity - 1,
            "the resumed sale moves stock exactly once");
        check(orders.resumeOrder(doneId) == "wrong-status",
            "a completed order cannot be resumed again");
    }

    // Phone and date rules, and the Order Edit load gate (v31).
    check(stringutils::isValidPhone("03001234567"), "phone: 11 plain digits accepted");
    check(stringutils::isValidPhone("0301-2345678"), "phone: dashed form accepted");
    check(!stringutils::isValidPhone("0301-234567"), "phone: 10 digits rejected");
    check(!stringutils::isValidPhone("0301-23456789"), "phone: 12 digits rejected");
    check(!stringutils::isValidPhone("030a-2345678"), "phone: letters rejected");
    check(datetime::isValidDate("2026-11-23"), "date: a real date accepted");
    check(!datetime::isValidDate("2026-13-40"), "date: impossible month/day rejected");
    check(!datetime::isValidDate("23-11-2026"), "date: wrong field order rejected");
    {
        CompletedBill peek;
        check(orders.loadPaidOrder(saved.order.id, peek).empty()
                && peek.order.id == saved.order.id,
            "a paid order can be LOADED without manager approval");
        check(orders.editPaidOrder(saved.order.id, false, peek) == "not-approved",
            "the edit gate itself still refuses without approval");
    }

    // ---- Batch A (v35) checks -------------------------------------
    // D-04: real calendar maths, not y*372+m*31+d.
    check(datetime::daysBetween("2026-04-30", "2026-05-07") == 7,
        "date: 30 Apr -> 7 May is really 7 days (fake maths said 8)");
    check(datetime::daysBetween("2026-02-28", "2026-03-06") == 6,
        "date: 28 Feb -> 6 Mar is really 6 days (fake maths said 9)");
    check(datetime::daysBetween("2026-02-28", "2026-03-01") == 1,
        "date: 28 Feb -> 1 Mar is 1 day");
    check(datetime::daysBetween("2026-11-26", "2026-11-26") == 0,
        "date: same day is 0 days apart");
    check(datetime::daysBetween("2026-13-40", "2026-11-26") == INT_MIN,
        "date: invalid date gives INT_MIN");

    // D-06: underpayment and bad status/merchant are refused, nothing
    // is silently rewritten to full payment.
    check(orders.addLine("111001", 1).empty(), "add 1x milk for underpayment test");
    CompletedBill under;
    check(orders.checkout("Under Test", "", "paid", "cash", "-", 0.0, 100.0, under)
            == "underpayment", "underpayment (100 on a 171 bill) refused");
    check(orders.checkout("Under Test", "", "paid", "cash", "-", 0.0, 0.0, under)
            == "underpayment", "amount received 0 is not silently full payment");
    check(orders.checkout("Under Test", "", "on-hold", "cash", "-", 0.0, 171.0, under)
            == "bad-status", "checkout with status on-hold writes no bill");
    check(orders.checkout("Under Test", "", "paid", "card", "HBL", 0.0, 171.0, under)
            == "bad-payment", "card merchant outside ABL/MCB/Meezan refused");
    check(orders.checkout("Under Test", "", "paid", "wallet", "JC", 0.0, 171.0, under)
            == "bad-payment", "wallet merchant outside JazzCash/EasyPaisa/Raast refused");
    check(orders.checkout("Under Test", "", "paid", "cash", "JazzCash", 0.0, 171.0, under)
            == "bad-payment", "cash with a merchant refused");
    check(RecordFile::readAllLines("Data/bill.txt").size() == 2,
        "refused checkouts wrote no bill");
    orders.clear();

    // D-06/D-22 in holdOrder too.
    check(orders.addLine("111001", 1).empty(), "add 1x milk for hold validation test");
    check(orders.holdOrder("Hold Bad", "", "wallet", "JC") == "bad-payment",
        "hold with a bad wallet merchant refused");
    orders.clear();

    // D-15: changeQuantity now reports instead of silently doing nothing.
    check(orders.addLine("111001", 1).empty(), "add 1x milk for changeQuantity test");
    check(orders.changeQuantity(0, 100) == "low-stock",
        "changeQuantity over stock is reported, not silent");
    check(orders.changeQuantity(0, 1).empty() && orders.lines().at(0).quantity == 2,
        "changeQuantity within stock succeeds");
    check(orders.changeQuantity(99, 1) == "bad-index",
        "changeQuantity out of range is reported");
    orders.clear();

    // D-20/D-21: barcode and product validation.
    check(CategoryService::makeBarcode("00", 1001).empty(),
        "barcode: category 00 refused");
    check(CategoryService::makeBarcode("11", 11001).empty(),
        "barcode: id 11001 cannot fit the 6-digit scheme (no % 10000 collision)");
    check(CategoryService::makeBarcode("11", 1001) == "111001",
        "barcode: full product id in the tail");
    check(categories.addCategory("00", "Zero", "ZERO") == "bad-id",
        "category 00 refused");
    {
        Product bad;
        bad.id = 1003; bad.categoryId = "11"; bad.name = "Tea";
        bad.acronym = "TEA500XXX";  // 9 chars, over the 8 limit
        bad.unit = "count"; bad.unitPrice = 50.0; bad.taxPerUnit = 0.0;
        bad.discountPerUnit = 0.0; bad.availableQuantity = 10;
        check(products.addProduct(bad) == "bad-fields",
            "product: acronym over 8 chars refused");
        bad.acronym = "TEA500";
        bad.unit = "litres";
        check(products.addProduct(bad) == "bad-fields",
            "product: unit outside count/weight refused");
        bad.unit = "count"; bad.unitPrice = -5.0;
        check(products.addProduct(bad) == "bad-fields",
            "product: negative price refused");
        bad.unitPrice = 50.0; bad.discountPerUnit = 60.0;
        check(products.addProduct(bad) == "bad-fields",
            "product: discount above price refused");
        bad.discountPerUnit = 0.0; bad.availableQuantity = -1;
        check(products.addProduct(bad) == "bad-fields",
            "product: negative quantity refused");
    }

    // D-17: overlapping active seasons and a 100% season are refused.
    check(seasonsService.addSeason("2026-01-01", "2026-12-31", 5.0, true) == "overlap",
        "season: overlapping an active season refused");
    check(seasonsService.addSeason("2026-01-01", "2026-12-31", 100.0, false) == "bad-percent",
        "season: 100% discount refused");

    // D-05: the manager PIN is checked by hash, never stored in plain
    // text. The correct PIN itself is never written in code - not even
    // in a test - so only rejection of wrong PINs is asserted here;
    // acceptance of the real PIN is exercised through the GUI prompt.
    check(pin_hash::fnv1a("0000") != pin_hash::kManagerPinHash,
        "pin: a wrong PIN hashes differently from the stored hash");
    check(!pin_hash::matchesManagerPin("0000"), "pin: wrong PIN does not match");
    check(!pin_hash::matchesManagerPin(""), "pin: empty PIN does not match");
    check(pin_hash::fnv1a("") == 2166136261u,
        "pin: FNV-1a of empty input is the offset basis");

    // D-19: a genuine Phase 2 product line (11 fields, no categoryId)
    // parses, with the category derived from the barcode.
    {
        const auto p2 = ProductSerialiser::parse(
            "1003 131000 TEA500 Tea count 50.00 0.00 0.00 10 barcodes/barcode_131000.bmp #");
        check(p2 && p2->categoryId == "13",
            "product: Phase 2 line parses, category 13 from barcode");
    }

    // D-01: two checkouts in the same second must still get distinct ids.
    {
        check(orders.addLine("111001", 1).empty(), "add 1x milk for id test 1");
        CompletedBill first;
        check(orders.checkout("Id Test 1", "", "paid", "cash", "-", 0.0, 171.0, first).empty(),
            "same-second checkout 1 succeeds");
        check(orders.addLine("111001", 1).empty(), "add 1x milk for id test 2");
        CompletedBill second;
        check(orders.checkout("Id Test 2", "", "paid", "cash", "-", 0.0, 171.0, second).empty(),
            "same-second checkout 2 succeeds");
        check(first.order.id != second.order.id,
            "same-second checkouts get distinct Order Ids");
        check(first.billId != second.billId,
            "same-second checkouts get distinct Bill Ids");
    }

    // D-03: a corrupt line is counted, reported, and backed up - not
    // silently deleted on the next save.
    {
        std::ofstream bad("Data/product.txt", std::ios::app);
        bad << "THIS IS NOT A PRODUCT LINE\n";
    }
    check(products.listAll().size() == 2, "dropped line: good products still load");
    check(productRepo.droppedLines() == 1, "dropped line: 1 unparsable line counted");
    check(productRepo.saveAll(products.listAll()), "dropped line: save succeeds");
    check(std::filesystem::exists("Data/product.txt.bak"),
        "dropped line: .bak copy written before the save");
    {
        bool bakHasBadLine = false;
        for (const std::string& line : RecordFile::readAllLines("Data/product.txt.bak"))
            if (line == "THIS IS NOT A PRODUCT LINE") bakHasBadLine = true;
        check(bakHasBadLine, "dropped line: .bak holds the unparsable line");
    }
    check(products.listAll().size() == 2 && productRepo.droppedLines() == 0,
        "dropped line: file is clean again after the save");

    // D-08/D-11: incomplete/completed reports are order-based, archiving
    // goes through the service, and the custom From>To error is explicit.
    check(orders.addLine("111001", 1).empty(), "add 1x milk for archive test");
    check(orders.holdOrder("Archive Me", "", "cash", "-").empty(), "hold for archive test");
    std::string archiveId;
    for (const std::string& line : RecordFile::readAllLines("Data/order.txt"))
        if (const auto o = OrderSerialiser::parse(line); o && o->status == "on-hold")
            archiveId = o->id;
    check(!archiveId.empty(), "held order exists to archive");
    check(reports.incompleteOrders().rows.size() == 1,
        "report: held order is visible in incomplete orders");
    check(orders.archiveOrder(archiveId).empty(), "archiveOrder succeeds");
    check(orders.archiveOrder("99999999") == "not-found",
        "archiveOrder on a missing order reports not-found");
    check(reports.incompleteOrders().rows.size() == 0,
        "report: archived order leaves incomplete orders");
    check(reports.completedOrders().rows.size() == 5,
        "report: completed orders lists the 4 paid + 1 archived order");
    {
        const ReportService::Table badRange = reports.cashSales("custom",
            "2026-12-31", "2026-01-01");
        check(badRange.rows.empty()
                && badRange.summary.rfind("Error:", 0) == 0,
            "report: custom From>To returns an error, not an empty table");
    }
    {
        const ReportService::Table sold = reports.topTenSoldItems();
        check(sold.rows.size() == 1 && sold.rows.at(0).at(1) == "MILK500"
                && sold.rows.at(0).at(2) == "4",
            "report: top sold aggregates by product over paid bills (MILK500 x4)");
        const ReportService::Table all = reports.allSalesWithDiscounts();
        check(all.rows.size() == 4
                && all.summary.find("Total discount:") != std::string::npos,
            "report: all sales lists paid bills only, with a total discount");
    }

    // D-02: the data is consistent - no orphans anywhere.
    {
        const DataHealth health = orders.dataHealth();
        check(health.ordersWithoutBill == 0 && health.billsWithoutOrder == 0
                && health.orphanBillDetails == 0,
            "data health: no paid order without a bill, no orphan bills/details");
    }

    // ---- Batch A (v37) journal checks --------------------------------
    // v37 (D-02): a normal checkout / edit leaves no pending entry,
    // and the journal snapshotted the stock before the sale.
    {
        JournalFileRepository journalRepo;
        const auto entries = journalRepo.loadAll();
        int pending = 0;
        for (const auto& e : entries) if (e.status == "pending") ++pending;
        check(entries.size() == 5, "journal: 5 entries (4 checkouts + 1 edit)");
        check(pending == 0, "journal: no pending entries after normal checkouts");
        check(journalRepo.droppedLines() == 0, "journal: no dropped lines");
        bool snapshotOk = false;
        for (const auto& e : entries)
        {
            if (e.orderId != bill.order.id || e.kind != "checkout") continue;
            for (const auto& l : e.lines)
                if (l.productId == 1001 && l.quantity == 2 && l.stockBefore == 40)
                    snapshotOk = true;
        }
        check(snapshotOk,
            "journal: first checkout snapshotted milk stock 40 before the sale");
    }

    // Simulate a crash mid-checkout: the order row and its detail row
    // were written, the bill was not, the stock never moved. The
    // pending journal entry is all recovery has to work from.
    // Bread x2: 240 + 10 tax - 20 discount = 230, seasonal 10% = 23,
    // so the customer owes 207.00.
    std::string crashOrderId, crashBillId, crashTime;
    {
        for (int tries = 0; tries < 6 && crashOrderId.empty(); ++tries)
        {
            const std::string cand = IdGenerator::newOrderId();
            bool used = false;
            for (const Order& o : orderRepo.loadAll())
                if (o.id == cand) used = true;
            if (!used) crashOrderId = cand;
            else std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        }
        check(!crashOrderId.empty(), "crash test: a fresh order id");
        for (int tries = 0; tries < 6 && crashBillId.empty(); ++tries)
        {
            const std::string cand = IdGenerator::newBillId();
            if (!billRepo.findById(cand)) crashBillId = cand;
            else std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        }
        check(!crashBillId.empty(), "crash test: a fresh bill id");
        crashTime = IdGenerator::nowTime();
        const auto breadBefore = products.findByBarcode("129876");
        check(breadBefore && breadBefore->availableQuantity == 25,
            "crash test: bread stock is 25 before the crash");
        Order crashOrder;
        crashOrder.id = crashOrderId;
        crashOrder.date = IdGenerator::todayDate();
        crashOrder.time = crashTime;
        crashOrder.customerName = "Crash Test";
        crashOrder.customerContact = "-";
        crashOrder.status = "paid";
        crashOrder.paymentAmount = 207.0;
        crashOrder.paymentMode = "cash";
        crashOrder.paymentMerchant = "-";
        check(orderRepo.append(crashOrder),
            "crash test: order row written (bill not yet)");
        check(detailRepo.append({crashOrderId, 1002, "129876", 2}),
            "crash test: detail row written (bill not yet)");
        JournalSerialiser::Entry e;
        e.orderId = crashOrderId; e.kind = "checkout"; e.billId = crashBillId;
        e.customerName = "Crash Test"; e.customerContact = "-";
        e.date = crashOrder.date; e.time = crashTime;
        e.paymentMode = "cash"; e.paymentMerchant = "-";
        e.seasonalAmount = 23.0; e.specialAmount = 0.0; e.amountReceived = 207.0;
        e.lines = {{1002, 2, 25}}; e.oldLines = {}; e.status = "pending";
        check(RecordFile::appendLine("Data/journal.txt", JournalSerialiser::format(e)),
            "crash test: pending journal entry written");
        check(!billRepo.findById(crashBillId),
            "crash test: no bill yet (the crash point)");
    }
    {
        const RecoveryReport r1 = orders.recoverJournal();
        check(r1.recovered == 1 && r1.recoveredOrderIds.size() == 1
                && r1.recoveredOrderIds.at(0) == crashOrderId && r1.skipped == 0
                && r1.droppedLines == 0,
            "recovery: 1 interrupted checkout recovered, order id reported");
        const auto rb = billRepo.findById(crashBillId);
        check(rb && near(rb->totalPayable, 207.0) && near(rb->seasonalDiscount, 23.0)
                && near(rb->amountReceived, 207.0),
            "recovery: bill rebuilt (payable 207, seasonal 23, received 207)");
        check(billDetailRepo.loadForBill(crashBillId).size() == 1,
            "recovery: bill detail row rebuilt (bread x2)");
        const auto breadAfter = products.findByBarcode("129876");
        check(breadAfter && breadAfter->availableQuantity == 23,
            "recovery: stock moved 25 -> 23");
        check(RecordFile::readAllLines("Data/bill.txt").size() == 5,
            "recovery: exactly one bill added (was 4)");
        const RecoveryReport r2 = orders.recoverJournal();
        check(r2.recovered == 0 && r2.skipped == 0,
            "recovery: second run recovers nothing (idempotent)");
        const auto breadAgain = products.findByBarcode("129876");
        check(breadAgain && breadAgain->availableQuantity == 23,
            "recovery: second run does not move stock again");
        check(billRepo.findById(crashBillId)
                && RecordFile::readAllLines("Data/bill.txt").size() == 5,
            "recovery: second run adds no duplicate bill");
        const DataHealth h2 = orders.dataHealth();
        check(h2.ordersWithoutBill == 0 && h2.billsWithoutOrder == 0
                && h2.orphanBillDetails == 0,
            "recovery: data health clean after the roll-forward");
    }

    // Simulate a crash mid-EDIT: the journal entry is pending but the
    // files still hold the pre-edit state (bread x2, bill 207). The
    // old bill row must be REWRITTEN to the edited totals, not kept.
    // Milk x1: 180 + 10 tax - 0 = 190, seasonal 10% = 19, so the
    // edited bill owes 171.00; amount received stays 207.00.
    {
        const auto oldBill = billRepo.findById(crashBillId);
        check(oldBill && near(oldBill->totalPayable, 207.0),
            "edit crash test: bill still holds the pre-edit totals");
        const auto milkBefore = products.findByBarcode("111001");
        check(milkBefore && milkBefore->availableQuantity == 36,
            "edit crash test: milk stock is 36 before the edit");
        JournalSerialiser::Entry e;
        e.orderId = crashOrderId; e.kind = "edit"; e.billId = crashBillId;
        e.customerName = "Crash Edited"; e.customerContact = "-";
        e.date = oldBill->billDate; e.time = oldBill->billTime;
        e.paymentMode = "cash"; e.paymentMerchant = "-";
        e.seasonalAmount = 19.0; e.specialAmount = 0.0; e.amountReceived = 207.0;
        e.lines = {{1001, 1, 36}}; e.oldLines = {{1002, 2, 23}}; e.status = "pending";
        check(RecordFile::appendLine("Data/journal.txt", JournalSerialiser::format(e)),
            "edit crash test: pending edit journal entry written");
        const RecoveryReport r3 = orders.recoverJournal();
        check(r3.recovered == 1 && r3.recoveredOrderIds.size() == 1
                && r3.recoveredOrderIds.at(0) == crashOrderId && r3.skipped == 0,
            "edit recovery: 1 interrupted edit recovered");
        const auto eb = billRepo.findById(crashBillId);
        check(eb && near(eb->totalPayable, 171.0) && near(eb->seasonalDiscount, 19.0)
                && near(eb->amountReceived, 207.0),
            "edit recovery: bill rewritten to the edited totals (171, seasonal 19)");
        const auto ed = billDetailRepo.loadForBill(crashBillId);
        check(ed.size() == 1 && ed.at(0).productAcronym == "MILK500"
                && ed.at(0).quantity == 1,
            "edit recovery: bill lines rewritten (milk x1 only)");
        std::optional<Order> editedOrder;
        for (const Order& o : orderRepo.loadAll())
            if (o.id == crashOrderId) editedOrder = o;
        check(editedOrder && editedOrder->customerName == "Crash Edited"
                && near(editedOrder->paymentAmount, 171.0),
            "edit recovery: order row updated (customer, amount 171)");
        const auto milkAfter = products.findByBarcode("111001");
        const auto breadAfterEdit = products.findByBarcode("129876");
        check(milkAfter && milkAfter->availableQuantity == 35
                && breadAfterEdit && breadAfterEdit->availableQuantity == 25,
            "edit recovery: stock reconciled (milk 36-1=35, bread 23+2=25)");
        const RecoveryReport r4 = orders.recoverJournal();
        check(r4.recovered == 0, "edit recovery: second run recovers nothing");
        const auto milkAgain = products.findByBarcode("111001");
        check(milkAgain && milkAgain->availableQuantity == 35,
            "edit recovery: second run does not move stock again");
    }

    // A malformed/partial journal line is a crash DURING the journal
    // write itself: ignored and counted, never guessed at.
    {
        std::ofstream bad("Data/journal.txt", std::ios::app);
        bad << "99999999 checkout 199999999 Half_Written\n";
        bad.close();  // flush to disk BEFORE recovery reads the file
        const RecoveryReport r5 = orders.recoverJournal();
        check(r5.droppedLines == 1 && r5.recovered == 0,
            "journal: malformed line ignored and counted as dropped");
        const RecoveryReport r6 = orders.recoverJournal();
        check(r6.droppedLines == 1 && r6.recovered == 0,
            "journal: dropped line is counted again on the next run, still ignored");
    }
    {
        JournalFileRepository journalRepo;
        const auto entries = journalRepo.loadAll();
        int pending = 0;
        for (const auto& e : entries) if (e.status == "pending") ++pending;
        check(pending == 0, "journal: no pending entries left at the end");
    }

    // The journal parser itself is strict: a truncated line, or junk
    // after the '#', is malformed - while a well-formed line parses.
    check(!JournalSerialiser::parse("99999999 checkout 199999999 Half_Written"),
        "journal: truncated line does not parse");
    check(!JournalSerialiser::parse(
            "12345678 checkout 112345678 A - 2026-11-26 12:00:00 cash - "
            "0.00 0.00 100.00 1001:1:40 - pending # extra"),
        "journal: junk after '#' does not parse");
    check(JournalSerialiser::parse(
            "12345678 checkout 112345678 A - 2026-11-26 12:00:00 cash - "
            "0.00 0.00 100.00 1001:1:40 - done #").has_value(),
        "journal: a well-formed done line parses");

    // ---- Batch B (v37, D-33) implied-opening stock audit -----------
    // opening = remaining stock + total sold (summed from
    // orderDetail.txt by product id). A product flags only when sold >
    // opening, i.e. its remaining stock would have to be negative.
    {
        const std::vector<OrderDetail> allDetails = detailRepo.loadAll();
        const std::map<int, int> sold = stock_audit::soldByProduct(allDetails);
        // The helper must agree with an independent sum over the file.
        // (A product with no detail rows has no key in the map - sold 0.)
        int milkSoldLoop = 0, breadSoldLoop = 0;
        for (const OrderDetail& d : allDetails)
        {
            if (d.productId == 1001) milkSoldLoop += d.quantity;
            if (d.productId == 1002) breadSoldLoop += d.quantity;
        }
        const auto milkIt = sold.find(1001);
        const auto breadIt = sold.find(1002);
        const int milkSoldMap = milkIt == sold.end() ? 0 : milkIt->second;
        const int breadSoldMap = breadIt == sold.end() ? 0 : breadIt->second;
        check(milkSoldMap == milkSoldLoop && breadSoldMap == breadSoldLoop,
            "stock audit: helper sold totals match an independent sum (milk, bread)");
        const auto milkEnd = products.findByBarcode("111001");
        check(milkEnd && sold.at(1001) == 6
                && stock_audit::impliedOpening(*milkEnd, sold.at(1001)) == 41
                && milkEnd->availableQuantity == 35,
            "stock audit: milk opening 41 = remaining 35 + sold 6");
        check(stock_audit::findViolations(products.listAll(), allDetails).empty(),
            "stock audit: no violations in the test data (all sold totals fit implied openings)");
    }
    {
        // The shipped OIL case, as a synthetic: remaining 8, sold 10
        // in one order - opening 18, consistent, no flag.
        Product oil;
        oil.id = 1007; oil.acronym = "OIL1L"; oil.availableQuantity = 8;
        const std::vector<OrderDetail> oilDetails = {
            {"13211318", 1007, "161122", 10}};
        check(stock_audit::impliedOpening(oil, 10) == 18
                && stock_audit::findViolations({oil}, oilDetails).empty(),
            "stock audit: OIL remaining 8 + sold 10 = opening 18, consistent");
        // The shipped OIL total is 11, not 10: a second paid order
        // (09263117) sold 1 more. Opening 19, still consistent.
        const std::vector<OrderDetail> oilAllDetails = {
            {"09263117", 1007, "161122", 1}, {"13211318", 1007, "161122", 10}};
        check(stock_audit::soldByProduct(oilAllDetails).at(1007) == 11
                && stock_audit::impliedOpening(oil, 11) == 19
                && stock_audit::findViolations({oil}, oilAllDetails).empty(),
            "stock audit: OIL total sold 11 (1 + 10) + remaining 8 = opening 19, consistent");
        // A genuinely inconsistent case must flag: remaining -2 with
        // 5 sold gives opening 3, and 5 > 3.
        Product bad;
        bad.id = 1007; bad.acronym = "OIL1L"; bad.availableQuantity = -2;
        const std::vector<OrderDetail> badDetails = {
            {"13211318", 1007, "161122", 5}};
        const std::vector<stock_audit::StockViolation> flags =
            stock_audit::findViolations({bad}, badDetails);
        check(flags.size() == 1 && flags.at(0).sold == 5
                && flags.at(0).opening == 3 && flags.at(0).remaining == -2,
            "stock audit: sold 5 > opening 3 (remaining -2) flags as inconsistent");
        // Boundary: sold exactly equals the opening (remaining 0,
        // sold 5, opening 5) is consistent, not a violation.
        Product soldOut;
        soldOut.id = 1016; soldOut.acronym = "CHOCO"; soldOut.availableQuantity = 0;
        const std::vector<OrderDetail> soldOutDetails = {
            {"13211318", 1016, "171016", 5}};
        check(stock_audit::impliedOpening(soldOut, 5) == 5
                && stock_audit::findViolations({soldOut}, soldOutDetails).empty(),
            "stock audit: sold 5 = opening 5 (remaining 0) does not flag");
    }

    // ---- Batch A (v38) checks -------------------------------------
    // D-06: acronyms are [A-Z0-9]{1,8} - a space, lowercase or
    // punctuation breaks the space-separated line, so it is refused.
    {
        Product bad;
        bad.id = 1003; bad.categoryId = "11"; bad.name = "Tea";
        bad.unit = "count"; bad.unitPrice = 50.0; bad.taxPerUnit = 0.0;
        bad.discountPerUnit = 0.0; bad.availableQuantity = 10;
        bad.acronym = "TEA 500";
        check(products.addProduct(bad) == "bad-fields",
            "acronym: a space in a product acronym is refused");
        bad.acronym = "tea500";
        check(products.addProduct(bad) == "bad-fields",
            "acronym: lowercase in a product acronym is refused");
        bad.acronym = "TEA-500";
        check(products.addProduct(bad) == "bad-fields",
            "acronym: punctuation in a product acronym is refused");
        bad.acronym = "TEA500XX";  // 8 chars, the longest valid form
        check(products.addProduct(bad).empty(),
            "acronym: a valid 8-char product acronym is still accepted");
        check(products.findByBarcode("111003")
                && products.findByBarcode("111003")->acronym == "TEA500XX",
            "acronym: the 8-char product loads back with its acronym");
    }
    check(categories.addCategory("19", "Test", "TE ST") == "bad-acronym",
        "acronym: a space in a category acronym is refused");
    check(categories.addCategory("19", "Test", "test") == "bad-acronym",
        "acronym: lowercase in a category acronym is refused");
    check(categories.updateCategory("11", "Dairy", "DA IRY") == "bad-acronym",
        "acronym: a space in an updated category acronym is refused");
    check(categories.addCategory("19", "TestCat", "TST").empty()
            && categories.findById("19")
            && categories.findById("19")->acronym == "TST",
        "acronym: a valid category acronym is still accepted");

    // D-08 (boundary, against the LIVE stock so the check survives
    // the sales above): one less than available sells, exactly
    // available does not - buying the last unit is refused.
    {
        const auto milkLive = products.findByBarcode("111001");
        const int avail = milkLive ? milkLive->availableQuantity : 0;
        check(milkLive && avail > 1
                && ProductService::canSell(*milkLive, avail - 1)
                && !ProductService::canSell(*milkLive, avail),
            "stock rule: available-1 sells, exactly available does not");
    }

    // D-23: resume a held order, then hold it AGAIN - the same order
    // is updated in place (same id, new customer/lines), not duplicated.
    std::string reholdId;
    {
        check(orders.addLine("111001", 1).empty(), "re-hold: add 1x milk");
        check(orders.holdOrder("Hold Again", "", "cash", "").empty(),
            "re-hold: first hold succeeds");
        for (const std::string& line : RecordFile::readAllLines("Data/order.txt"))
            if (const auto o = OrderSerialiser::parse(line);
                o && o->status == "on-hold" && o->customerName == "Hold Again")
                reholdId = o->id;
        check(!reholdId.empty(), "re-hold: the held order exists");
        check(orders.resumeOrder(reholdId).empty() && orders.lines().size() == 1,
            "re-hold: resume reloads the held order's lines");
        check(orders.addLine("129876", 1).empty(), "re-hold: add 1x bread to the resumed cart");
        const auto milkBeforeRehold = products.findByBarcode("111001");
        const auto breadBeforeRehold = products.findByBarcode("129876");
        check(orders.holdOrder("Hold Again 2", "", "cash", "").empty(),
            "re-hold: holding the resumed order again succeeds");
        int onHoldRows = 0;
        bool sameIdOnHold = false;
        std::string reholdCustomer;
        for (const std::string& line : RecordFile::readAllLines("Data/order.txt"))
            if (const auto o = OrderSerialiser::parse(line); o && o->status == "on-hold")
            {
                ++onHoldRows;
                if (o->id == reholdId)
                {
                    sameIdOnHold = true;
                    reholdCustomer = o->customerName;
                }
            }
        check(onHoldRows == 1 && sameIdOnHold,
            "re-hold: still exactly 1 on-hold row, same Order Id (no duplicate)");
        check(reholdCustomer == "Hold Again 2",
            "re-hold: the in-place update carries the new customer name");
        const std::vector<OrderDetail> reholdDetails =
            detailRepo.loadForOrder(reholdId);
        bool reholdLinesMatch = reholdDetails.size() == 2;
        {
            int milkQty = 0;
            int breadQty = 0;
            for (const OrderDetail& d : reholdDetails)
            {
                if (d.productId == 1001) milkQty += d.quantity;
                if (d.productId == 1002) breadQty += d.quantity;
            }
            reholdLinesMatch = reholdLinesMatch && milkQty == 1 && breadQty == 1;
        }
        check(reholdLinesMatch,
            "re-hold: detail rows replaced with the current cart (milk x1, bread x1)");
        const auto milkAfterRehold = products.findByBarcode("111001");
        const auto breadAfterRehold = products.findByBarcode("129876");
        check(milkBeforeRehold && milkAfterRehold
                && milkAfterRehold->availableQuantity == milkBeforeRehold->availableQuantity
                && breadBeforeRehold && breadAfterRehold
                && breadAfterRehold->availableQuantity == breadBeforeRehold->availableQuantity,
            "re-hold: holding moves no stock");
        // D-23 KEEP: m_resumedOrderId is still set after the in-place
        // re-hold, so a checkout of a FRESH cart (no resume in
        // between) must still supersede the held order in place.
        // Milk x1: 180 + 10 tax - 0 = 190, seasonal 10% = 19, so the
        // customer owes 171.00.
        const std::size_t orderRowsBefore = RecordFile::readAllLines("Data/order.txt").size();
        check(orders.addLine("111001", 1).empty(), "re-hold: add 1x milk to a fresh cart");
        CompletedBill reholdBill;
        check(orders.checkout("Hold Again 2", "", "paid", "cash", "-", 0.0, 171.0, reholdBill).empty(),
            "re-hold: checkout after a re-hold (no resume) succeeds");
        check(reholdBill.order.id == reholdId,
            "re-hold: that checkout supersedes the held order (same Order Id)");
        check(RecordFile::readAllLines("Data/order.txt").size() == orderRowsBefore,
            "re-hold: the superseding checkout adds no order row");
        bool reholdPaid = false;
        for (const std::string& line : RecordFile::readAllLines("Data/order.txt"))
            if (const auto o = OrderSerialiser::parse(line);
                o && o->id == reholdId && o->status == "paid")
                reholdPaid = true;
        check(reholdPaid, "re-hold: the held order's row is now paid, no ghost on-hold left");
    }

    // D-26a: an anonymous hold is stored as "Walk-in" - but a fresh
    // checkout with an empty customer box is still refused.
    {
        check(orders.addLine("129876", 1).empty(), "walk-in: add 1x bread for an anonymous hold");
        check(orders.holdOrder("", "", "cash", "").empty(),
            "walk-in: holding with no customer name succeeds");
        std::string walkInCustomer;
        for (const std::string& line : RecordFile::readAllLines("Data/order.txt"))
            if (const auto o = OrderSerialiser::parse(line); o && o->status == "on-hold")
                walkInCustomer = o->customerName;
        check(walkInCustomer == "Walk-in",
            "walk-in: the anonymous hold is stored as 'Walk-in'");
        const auto breadAfterHold = products.findByBarcode("129876");
        check(breadAfterHold && breadAfterHold->availableQuantity == 25,
            "walk-in: the anonymous hold moves no stock");
        check(orders.addLine("111001", 1).empty(), "walk-in: add 1x milk for a nameless checkout");
        CompletedBill nameless;
        check(orders.checkout("", "", "paid", "cash", "-", 0.0, 171.0, nameless)
                == "no-customer",
            "walk-in: a fresh checkout with no customer name is still refused");
        orders.clear();
    }

    // D-10 (renumbered D-22 in the audit): the COMBINED seasonal +
    // special discount is capped at 50% of the pre-discount payable.
    {
        Totals base;
        base.withoutTax = 1000.0; base.tax = 0.0; base.discount = 0.0;
        base.payable = 1000.0;
        check(near(BillCalculator::payableAfterDiscounts(base, 990.0, 80.0), 500.0),
            "discount cap: 99% season + 8% special on 1000 pays exactly 500, never 0");
        double seasonalEff = 0.0;
        double specialEff = 0.0;
        BillCalculator::cappedDiscounts(base, 990.0, 80.0, seasonalEff, specialEff);
        check(near(seasonalEff, 500.0) && near(specialEff, 0.0),
            "discount cap: the cap trims the special first (500 seasonal, 0 special)");
        check(near(BillCalculator::payableAfterDiscounts(base, 49.5, 0.0), 950.5)
                && near(BillCalculator::payableAfterDiscounts(base, 10.0, 8.0), 982.0),
            "discount cap: normal small discounts are unaffected");
        check(near(BillCalculator::payableAfterDiscounts(base, 400.0, 100.0), 500.0),
            "discount cap: a combined discount of exactly 50% is untouched");
        BillService billService(billRepo, billDetailRepo);
        Order capOrder;
        capOrder.id = "19999999"; capOrder.customerName = "Cap Test";
        capOrder.date = IdGenerator::todayDate(); capOrder.time = IdGenerator::nowTime();
        capOrder.status = "paid"; capOrder.paymentMode = "cash"; capOrder.paymentMerchant = "-";
        const auto capBill = billService.buildBill(capOrder, base,
            "199999999", 990.0, 80.0, 500.0);
        check(capBill && near(capBill->totalPayable, 500.0)
                && near(capBill->seasonalDiscount, 500.0)
                && near(capBill->specialDiscount, 0.0)
                && near(capBill->totalWithoutTax + capBill->totalTax
                    - capBill->totalDiscount - capBill->seasonalDiscount
                    - capBill->specialDiscount, capBill->totalPayable),
            "discount cap: the bill builds with capped stored discounts that sum to its payable");
    }

    // D-02a: each manager approval is logged to Data/audit.txt.
    {
        check(audit_log::logManagerApproval("12345678", "2026-11-26", "12:34:56"),
            "audit log: an approval append succeeds");
        const auto auditLines = RecordFile::readAllLines("Data/audit.txt");
        check(auditLines.size() == 1
                && auditLines.at(0) == "12345678 2026-11-26 12:34:56 #",
            "audit log: the line is '<orderId> <date> <time> #'");
        check(!audit_log::logManagerApproval(""),
            "audit log: an empty order id is refused");
        check(audit_log::logManagerApproval("12345679", "2026-11-26", "12:35:00"),
            "audit log: a second approval appends");
        check(RecordFile::readAllLines("Data/audit.txt").size() == 2,
            "audit log: two approvals are two lines");
        check(audit_log::logManagerApproval("12345680")
                && RecordFile::readAllLines("Data/audit.txt").size() == 3,
            "audit log: the dateless form stamps today and appends");
        const auto auditLines3 = RecordFile::readAllLines("Data/audit.txt");
        check(auditLines3.at(2).rfind("12345680 " + IdGenerator::todayDate(), 0) == 0,
            "audit log: the dateless form uses today's date");
    }

    // ---- Batch B (v38) checks -------------------------------------
    // D-21: the service refuses a duplicate id on add ("bad-id") -
    // the GUI used to bypass that by switching to update when nothing
    // was loaded; it now refuses in the GUI too (see the batch
    // report for the screenshot proof).
    {
        Product dup;
        dup.id = 1001; dup.categoryId = "11"; dup.name = "Dup Milk";
        dup.acronym = "DUP"; dup.unit = "count"; dup.unitPrice = 10.0;
        dup.taxPerUnit = 0.0; dup.discountPerUnit = 0.0;
        dup.availableQuantity = 1;
        check(products.addProduct(dup) == "bad-id",
            "duplicate id: addProduct refuses an id that already exists");
        check(products.findByBarcode("111001")
                && products.findByBarcode("111001")->name == "Fresh Milk",
            "duplicate id: the refused add leaves the original product untouched");
    }

    // D-18: Order Edit appends a line - the service side the GUI's
    // new Add button flows through. The paid order (bill.order.id)
    // currently holds milk x1 (payable 171.00 after the earlier edit);
    // appending bread x1 must re-issue the same bill with the new
    // payable. Milk 180 + bread 120 = 300 without tax, tax 15,
    // discount 10 -> payable 305, seasonal 10% = 30.50, so the bill
    // is 274.50.
    {
        const std::string editId = bill.order.id;
        check(detailRepo.loadForOrder(editId).size() == 1,
            "edit-append: the paid order holds 1 line before the append");
        const auto breadBefore = products.findByBarcode("129876");
        check(breadBefore && ProductService::canSell(*breadBefore, 1)
                && ProductService::canSell(*breadBefore, 2),
            "edit-append: the GUI stock gate (canSell, strict <) accepts bread x1 and x2");
        const std::size_t billRowsBefore =
            RecordFile::readAllLines("Data/bill.txt").size();
        // Stock before the append (captured live: the middle sections
        // run more checkouts than the opening trace suggests).
        const auto milkBeforeAppend = products.findByBarcode("111001");
        const auto breadBeforeAppend = products.findByBarcode("129876");
        const int milkStockBefore =
            milkBeforeAppend ? milkBeforeAppend->availableQuantity : -1;
        const int breadStockBefore =
            breadBeforeAppend ? breadBeforeAppend->availableQuantity : -1;
        std::vector<CartLine> appended;
        appended.push_back({*products.findByBarcode("111001"), 1});
        appended.push_back({*products.findByBarcode("129876"), 1});
        CompletedBill appendedBill;
        check(orders.saveEditedPaidOrder(editId, "Edited Customer", "",
            appended, true, appendedBill).empty(),
            "edit-append: saving the paid order with an appended line succeeds");
        check(appendedBill.bill.id == bill.billId
                && near(appendedBill.bill.totalPayable, 274.50),
            "edit-append: the same Bill Id is re-issued, payable 305 - 30.50 seasonal = 274.50");
        check(RecordFile::readAllLines("Data/bill.txt").size() == billRowsBefore,
            "edit-append: the bill is rewritten in place (no new bill row)");
        check(detailRepo.loadForOrder(editId).size() == 2,
            "edit-append: the order now holds 2 detail rows (milk x1, bread x1)");
        const auto milkAfterAppend = products.findByBarcode("111001");
        const auto breadAfterAppend = products.findByBarcode("129876");
        check(milkAfterAppend && milkAfterAppend->availableQuantity == milkStockBefore
                && breadAfterAppend
                && breadAfterAppend->availableQuantity == breadStockBefore - 1,
            "edit-append: stock reconciled (milk returned then re-taken, bread -1)");
    }

    // D-23 GUI flow: resume -> hold -> clear() -> fresh checkout.
    // clear() is exactly what the GUI's posNewOrder() does (called by
    // posHold() after a hold and by the New Order button), so this is
    // the GUI sequence at service level. The kept m_resumedOrderId
    // must NOT survive the clear: the fresh checkout creates a NEW
    // order and the re-held order stays on-hold untouched.
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        check(orders.addLine("111001", 1).empty(), "gui-flow: add 1x milk for the first hold");
        check(orders.holdOrder("GUI Flow", "", "cash", "").empty(),
            "gui-flow: first hold succeeds");
        std::string guiHeldId;
        for (const std::string& line : RecordFile::readAllLines("Data/order.txt"))
            if (const auto o = OrderSerialiser::parse(line);
                o && o->status == "on-hold" && o->customerName == "GUI Flow")
                guiHeldId = o->id;
        check(!guiHeldId.empty(), "gui-flow: the held order exists");
        check(orders.resumeOrder(guiHeldId).empty() && orders.lines().size() == 1,
            "gui-flow: resume reloads the held order's lines");
        check(orders.addLine("129876", 1).empty(), "gui-flow: add 1x bread to the resumed cart");
        check(orders.holdOrder("GUI Flow 2", "", "cash", "").empty(),
            "gui-flow: re-holding the resumed order succeeds");
        int onHoldAfterRehold = 0;
        for (const std::string& line : RecordFile::readAllLines("Data/order.txt"))
            if (const auto o = OrderSerialiser::parse(line);
                o && o->status == "on-hold" && o->id == guiHeldId)
                ++onHoldAfterRehold;
        check(onHoldAfterRehold == 1,
            "gui-flow: after the re-hold there is exactly 1 on-hold row with the same id");
        // posNewOrder(): the GUI clears the cart (and the kept id).
        orders.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        const std::size_t orderRowsBefore =
            RecordFile::readAllLines("Data/order.txt").size();
        check(orders.addLine("111001", 1).empty(), "gui-flow: add 1x milk to a fresh cart");
        CompletedBill freshBill;
        check(orders.checkout("Fresh Buyer", "", "paid", "cash", "-", 0.0, 171.0,
            freshBill).empty(), "gui-flow: a fresh checkout after the clear succeeds");
        check(!freshBill.order.id.empty() && freshBill.order.id != guiHeldId,
            "gui-flow: the fresh checkout creates a NEW order (no stale resumed id)");
        check(RecordFile::readAllLines("Data/order.txt").size() == orderRowsBefore + 1,
            "gui-flow: the fresh checkout adds exactly one order row");
        bool heldStillThere = false;
        int onHoldTotal = 0;
        for (const std::string& line : RecordFile::readAllLines("Data/order.txt"))
            if (const auto o = OrderSerialiser::parse(line); o && o->status == "on-hold")
            {
                ++onHoldTotal;
                if (o->id == guiHeldId) heldStillThere = true;
            }
        check(heldStillThere && onHoldTotal == 2,
            "gui-flow: the re-held order is still on-hold (2 on-hold rows: it and the Walk-in hold)");
    }

    std::cout << (failures == 0 ? "ALL POS TESTS PASSED\n" : "SOME TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
