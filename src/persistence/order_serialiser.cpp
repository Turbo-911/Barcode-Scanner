#include "persistence/order_serialiser.h"
#include "common/string_utils.h"

#include <sstream>

std::optional<Order> OrderSerialiser::parse(const std::string& line)
{
    std::istringstream iss(line);
    Order o;
    std::string marker;
    if (!(iss >> o.id >> o.date >> o.time >> o.customerName >> o.customerContact
             >> o.status >> o.paymentAmount >> o.paymentMode >> o.paymentMerchant >> marker))
        return std::nullopt;
    if (marker != "#") return std::nullopt;
    o.customerName = stringutils::decode(o.customerName);
    o.customerContact = stringutils::decode(o.customerContact);
    o.paymentMerchant = stringutils::decode(o.paymentMerchant);
    return o;
}

std::string OrderSerialiser::format(const Order& o)
{
    std::ostringstream oss;
    oss << o.id << ' ' << o.date << ' ' << o.time << ' '
        << stringutils::encode(o.customerName) << ' '
        << stringutils::encode(o.customerContact) << ' '
        << o.status << ' ' << stringutils::money(o.paymentAmount) << ' '
        << o.paymentMode << ' ' << stringutils::encode(o.paymentMerchant) << " #";
    return oss.str();
}
