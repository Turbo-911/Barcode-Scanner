#pragma once
#include "domain/bill.h"
#include "domain/order.h"
#include "domain/product.h"
#include "persistence/i_bill_detail_repo.h"
#include "persistence/i_bill_repo.h"
#include "persistence/i_order_repo.h"
#include "persistence/i_product_repo.h"
#include <string>
#include <vector>

class ReportService
{
public:
    struct Table
    {
        std::string title;
        std::vector<std::string> headers;
        std::vector<std::vector<std::string>> rows;
        std::string summary;  // e.g. "Total: PKR 1234.56"
    };

    ReportService(IOrderRepository& orders, IBillRepository& bills,
        IBillDetailRepository& details, IProductRepository& products)
        : m_orders(orders), m_bills(bills), m_details(details), m_products(products) {}

    Table incompleteOrders();
    Table completedOrders();

    Table cashSales(const std::string& mode, const std::string& fromDate,
        const std::string& toDate);

    Table allSalesWithDiscounts();

    Table topTenSoldItems();
    Table topTenRemainingStock();

private:
    IOrderRepository& m_orders;
    IBillRepository& m_bills;
    IBillDetailRepository& m_details;
    IProductRepository& m_products;
};