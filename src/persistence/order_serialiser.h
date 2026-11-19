#pragma once
#include "domain/order.h"
#include <optional>
#include <string>

// OrderSerialiser — ONE Order <-> ONE Data/order.txt line (SRP):
//   orderId date time customerName contact status paymentAmount mode merchant #
class OrderSerialiser
{
public:
    static std::optional<Order> parse(const std::string& line);
    static std::string format(const Order& order);
};
