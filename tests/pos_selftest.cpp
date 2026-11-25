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
#include "persistence/i_order_repo.h"
#include "persistence/i_bill_repo.h"
#include "persistence/i_bill_detail_repo.h"
#include "persistence/i_category_repo.h"
#include "persistence/i_seasons_repo.h"
#include "persistence/i_product_repo.h"
#include "persistence/bill_serialiser.h"
#include "persistence/bill_detail_serialiser.h"
#include "persistence/order_detail_serialiser.h"
#include "persistence/order_serialiser.h"
#include "persistence/record_file.h"
#include "services/category_service.h"
#include "services/id_generator.h"
#include "services/order_service.h"
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
    check(ProductService::canSell(*milk, 40) && !ProductService::canSell(*milk, 41)
        && !ProductService::canSell(*milk, 0), "stock rule: qty must be 1..available");
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

    std::cout << (failures == 0 ? "ALL POS TESTS PASSED\n" : "SOME TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
