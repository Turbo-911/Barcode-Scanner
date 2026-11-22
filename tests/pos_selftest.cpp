// pos_selftest.cpp - headless test for the POS modules (Phases 2 + 3).
// Run it from an EMPTY folder: it seeds its own Data files there, runs
// checkout / hold / resume / paid-order-edit cycles through the real
// services and repositories, and checks the Data/*.txt files produced.
// Build (from the project root):
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

#include <chrono>
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
    {
        std::filesystem::create_directories("Data");
        std::ofstream cats("Data/PCategory.txt");
        cats << "11 Dairy MLK #\n";
        cats << "12 Bakery BAK #\n";
        std::ofstream seed("Data/product.txt");
        seed << "1001 111001 MILK500 Fresh_Milk count 180.00 10.00 0.00 40 tests/barcode_111001.bmp 11 #\n";
        seed << "1002 129876 BREAD Branded_Bread count 120.00 5.00 10.00 25 tests/barcode_129876.bmp 12 #\n";
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

    std::cout << (failures == 0 ? "ALL POS TESTS PASSED\n" : "SOME TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
