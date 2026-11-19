#pragma once
#include "domain/order.h"
#include <optional>
#include <string>

// OrderDetailSerialiser — ONE OrderDetail <-> ONE Data/orderDetail.txt line (SRP):
//   orderId productId productBarcode quantity #
class OrderDetailSerialiser
{
public:
    static std::optional<OrderDetail> parse(const std::string& line);
    static std::string format(const OrderDetail& detail);
};
