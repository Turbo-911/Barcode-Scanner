// pos_selftest.cpp - headless test for the Phase 2 POS modules.
// Run it from an EMPTY folder: it seeds its own Data/product.txt there,
// runs a full order -> bill cycle through the real services and
// repositories, and checks the Data/*.txt files it produces.
// Build (from the project root):
//   g++ -std=c++17 -Isrc tests/pos_selftest.cpp src/persistence/*.cpp src/services/*.cpp src/domain/*.cpp src/common/string_utils.cpp src/common/date_time.cpp -o pos_selftest
#include "persistence/i_order_repo.h"
#include "persistence/i_bill_repo.h"
#include "persistence/i_product_repo.h"
#include "persistence/bill_serialiser.h"
#include "persistence/order_detail_serialiser.h"
#include "persistence/order_serialiser.h"
#include "persistence/record_file.h"
#include "services/id_generator.h"
#include "services/order_service.h"
#include "common/string_utils.h"

#include <cmath>
#include <filesystem>
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
        std::ofstream seed("Data/product.txt");
        seed << "1001 123456 MILK500 Fresh_Milk count 180.00 10.00 0.00 40 tests/barcode_123456.bmp #\n";
        seed << "1002 987654 BREAD Branded_Bread count 120.00 5.00 10.00 25 tests/barcode_987654.bmp #\n";
    }

    ProductFileRepository productRepo;
    OrderFileRepository orderRepo;
    OrderDetailFileRepository detailRepo;
    BillFileRepository billRepo;
    ProductService products(productRepo);
    OrderService orders(products, orderRepo, detailRepo, billRepo);

    const auto milk = products.findByBarcode("123456");
    check(milk && milk->name == "Fresh Milk" && near(milk->unitPrice, 180.0),
        "product lookup by barcode, name '_' decoded to space");
    check(!products.findByBarcode("654321"), "unknown barcode not found");
    check(stringutils::isValidPhone("03001234567"), "phone rule: 11 digits accepted");
    check(!stringutils::isValidPhone("0300123456"), "phone rule: 10 digits rejected");
    check(!stringutils::isValidPhone("0300-123456"), "phone rule: dashes rejected");
    check(ProductService::canSell(*milk, 40) && !ProductService::canSell(*milk, 41)
        && !ProductService::canSell(*milk, 0), "stock rule: qty must be 1..available");

    check(orders.addLine("123456", 2).empty(), "add 2x milk");
    check(orders.addLine("987654", 1).empty(), "add 1x bread");
    check(orders.addLine("123456", 39) == "low-stock", "merged qty 41 > stock 40 rejected");
    check(orders.addLine("654321", 1) == "not-found", "unknown product rejected");

    const Totals t = orders.totals();
    check(near(t.withoutTax, 480.0) && near(t.tax, 25.0) && near(t.discount, 10.0)
        && near(t.payable, 495.0), "totals: 480 + 25 tax - 10 discount = 495 payable");

    CompletedBill bill;
    check(orders.checkout("Test Customer", "", "paid", "cash", "", bill).empty(),
        "checkout succeeds");
    check(bill.order.id.size() == 8 && bill.billId.size() == 9, "order id 8 digits, bill id 9 digits");
    check(bill.order.id.substr(6) == IdGenerator::todayDate().substr(8),
        "order id ends with today's day (date of order)");

    const auto orderLines = RecordFile::readAllLines("Data/order.txt");
    check(orderLines.size() == 1, "Data/order.txt has exactly 1 line");
    const auto parsedOrder = OrderSerialiser::parse(orderLines.at(0));
    check(parsedOrder && parsedOrder->customerName == "Test Customer"
        && near(parsedOrder->paymentAmount, 495.0) && parsedOrder->status == "paid",
        "Data/order.txt round-trips (name, amount, status)");

    const auto detailLines = RecordFile::readAllLines("Data/orderDetail.txt");
    check(detailLines.size() == 2, "Data/orderDetail.txt has 2 lines");
    const auto d0 = OrderDetailSerialiser::parse(detailLines.at(0));
    check(d0 && d0->orderId == bill.order.id && d0->quantity == 2,
        "OrderDetail row 1 matches the order and quantity");

    const auto billLines = RecordFile::readAllLines("Data/bill.txt");
    check(billLines.size() == 2, "Data/bill.txt has one line per product (2)");
    const auto b0 = BillSerialiser::parse(billLines.at(0));
    check(b0 && b0->id == bill.billId && near(b0->totalWithoutTax, 480.0)
        && near(b0->totalTax, 25.0) && near(b0->totalDiscount, 10.0)
        && near(b0->totalPayable, 495.0) && b0->paymentStatus == "paid",
        "Data/bill.txt row carries id, totals and payment status");

    const auto after = products.findByBarcode("123456");
    check(after && after->availableQuantity == 38, "stock decremented 40 -> 38 after sale");

    std::cout << (failures == 0 ? "ALL POS TESTS PASSED\n" : "SOME TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
