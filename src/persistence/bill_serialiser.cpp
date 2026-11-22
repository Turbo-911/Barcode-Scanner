#include "persistence/bill_serialiser.h"
#include "common/string_utils.h"

#include <sstream>

std::optional<Bill> BillSerialiser::parse(const std::string& line)
{
    std::istringstream iss(line);
    Bill b;
    std::string marker;
    if (!(iss >> b.id >> b.customerName >> b.orderId
             >> b.totalWithoutTax >> b.totalTax >> b.totalDiscount
             >> b.totalPayable >> b.amountReceived >> b.balanceReturned
             >> b.paymentStatus >> b.seasonalDiscount >> b.specialDiscount
             >> b.billDate >> b.billTime >> marker))
        return std::nullopt;
    if (marker != "#") return std::nullopt;
    b.customerName = stringutils::decode(b.customerName);
    return b;
}

std::string BillSerialiser::format(const Bill& b)
{
    std::ostringstream oss;
    oss << b.id << ' ' << stringutils::encode(b.customerName) << ' '
        << b.orderId << ' '
        << stringutils::money(b.totalWithoutTax) << ' '
        << stringutils::money(b.totalTax) << ' '
        << stringutils::money(b.totalDiscount) << ' '
        << stringutils::money(b.totalPayable) << ' '
        << stringutils::money(b.amountReceived) << ' '
        << stringutils::money(b.balanceReturned) << ' '
        << b.paymentStatus << ' '
        << stringutils::money(b.seasonalDiscount) << ' '
        << stringutils::money(b.specialDiscount) << ' '
        << b.billDate << ' ' << b.billTime << " #";
    return oss.str();
}