#include "persistence/order_detail_serialiser.h"

#include <sstream>

std::optional<OrderDetail> OrderDetailSerialiser::parse(const std::string& line)
{
    std::istringstream iss(line);
    OrderDetail d;
    std::string marker;
    if (!(iss >> d.orderId >> d.productId >> d.productBarcode >> d.quantity >> marker))
        return std::nullopt;
    if (marker != "#") return std::nullopt;
    return d;
}

std::string OrderDetailSerialiser::format(const OrderDetail& d)
{
    std::ostringstream oss;
    oss << d.orderId << ' ' << d.productId << ' ' << d.productBarcode << ' '
        << d.quantity << " #";
    return oss.str();
}
