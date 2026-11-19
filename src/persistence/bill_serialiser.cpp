#include "persistence/bill_serialiser.h"
#include "common/string_utils.h"

#include <sstream>

std::optional<Bill> BillSerialiser::parse(const std::string& line)
{
    std::istringstream iss(line);
    Bill b;
    std::string marker;
    if (!(iss >> b.id >> b.customerName >> b.orderId >> b.productAcronym
             >> b.unitPrice >> b.discountPerUnit >> b.quantity
             >> b.totalWithoutTax >> b.totalTax >> b.totalDiscount
             >> b.totalPayable >> b.paymentStatus >> marker))
        return std::nullopt;
    if (marker != "#") return std::nullopt;
    b.customerName = stringutils::decode(b.customerName);
    return b;
}

std::string BillSerialiser::format(const Bill& b)
{
    std::ostringstream oss;
    oss << b.id << ' ' << stringutils::encode(b.customerName) << ' ' << b.orderId << ' '
        << b.productAcronym << ' ' << stringutils::money(b.unitPrice) << ' '
        << stringutils::money(b.discountPerUnit) << ' ' << b.quantity << ' '
        << stringutils::money(b.totalWithoutTax) << ' '
        << stringutils::money(b.totalTax) << ' '
        << stringutils::money(b.totalDiscount) << ' '
        << stringutils::money(b.totalPayable) << ' '
        << b.paymentStatus << " #";
    return oss.str();
}
