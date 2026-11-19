// seed_history.cpp — creates a realistic "mid-running" history for the
// Chili Mili POS: 5 past orders with their OrderDetail rows and bills,
// written through the project's OWN repositories, serialisers and
// BillCalculator (no hand-typed file lines, so formats cannot drift).
// Run from the project root AFTER the app has been built once:
//   g++ -std=c++17 -Isrc tests/seed_history.cpp src/persistence/*.cpp src/services/*.cpp src/domain/*.cpp src/common/string_utils.cpp src/common/date_time.cpp -o seed_history
//   ./seed_history        (writes Data/order.txt, Data/orderDetail.txt, Data/bill.txt)
// Stock in Data/product.txt is NOT changed: it already reflects today's shelf.
#include "persistence/i_order_repo.h"
#include "persistence/i_bill_repo.h"
#include "persistence/i_product_repo.h"
#include "services/bill_calculator.h"
#include "services/product_service.h"

#include <iostream>
#include <vector>

namespace
{
struct SeedOrder
{
    std::string id, billId, date, time, customer, contact, status, mode, merchant;
    std::vector<CartLine> lines;
};
}

int main()
{
    ProductFileRepository productRepo;
    OrderFileRepository orderRepo;
    OrderDetailFileRepository detailRepo;
    BillFileRepository billRepo;
    ProductService products(productRepo);

    auto line = [&](const char* barcode, int qty) {
        auto p = products.findByBarcode(barcode);
        if (!p) { std::cerr << "missing product " << barcode << "\n"; std::exit(1); }
        return CartLine{*p, qty};
    };

    const std::vector<SeedOrder> seeds = {
        {"10152214", "110152214", "2026-11-14", "10:15:22", "Ahmed Raza", "0301-2345678",
            "paid", "cash", "", {line("123456", 2), line("987654", 1)}},
        {"13400515", "113400515", "2026-11-15", "13:40:05", "Fatima Noor", "",
            "paid", "card", "ABL", {line("200001", 1), line("555555", 2)}},
        {"18024716", "118024716", "2026-11-16", "18:02:47", "Usman Tariq", "0322-9876543",
            "on-hold", "wallet", "JazzCash", {line("999999", 1), line("246810", 4)}},
        {"09263117", "109263117", "2026-11-17", "09:26:31", "Ayesha Khan", "0300-5551234",
            "paid", "wallet", "EasyPaisa", {line("112233", 1), line("100000", 2), line("123456", 1)}},
        {"11480918", "111480918", "2026-11-18", "11:48:09", "Bilal Ahmed", "",
            "in-progress", "cash", "", {line("334455", 3), line("445566", 1)}},
    };

    for (const SeedOrder& seed : seeds)
    {
        const Totals t = BillCalculator::calculate(seed.lines);
        Order order;
        order.id = seed.id; order.date = seed.date; order.time = seed.time;
        order.customerName = seed.customer; order.customerContact = seed.contact;
        order.status = seed.status; order.paymentAmount = t.payable;
        order.paymentMode = seed.mode; order.paymentMerchant = seed.merchant;
        if (!orderRepo.append(order)) { std::cerr << "order write failed\n"; return 1; }
        for (const CartLine& l : seed.lines)
        {
            detailRepo.append({seed.id, l.product.id, l.product.barcode, l.quantity});
            Bill b;
            b.id = seed.billId; b.customerName = seed.customer; b.orderId = seed.id;
            b.productAcronym = l.product.acronym; b.unitPrice = l.product.unitPrice;
            b.discountPerUnit = l.product.discountPerUnit; b.quantity = l.quantity;
            b.totalWithoutTax = t.withoutTax; b.totalTax = t.tax;
            b.totalDiscount = t.discount; b.totalPayable = t.payable;
            b.paymentStatus = seed.status;
            billRepo.append(b);
        }
        std::cout << "seeded order " << seed.id << " (" << seed.date << ") payable "
                  << t.payable << "\n";
    }
    std::cout << "History seeded: " << seeds.size() << " orders.\n";
    return 0;
}
