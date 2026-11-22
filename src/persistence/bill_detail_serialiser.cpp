#include "persistence/bill_detail_serialiser.h"
#include "common/string_utils.h"

#include <sstream>

std::optional<BillDetail> BillDetailSerialiser::parse(const std::string& line)
{
    std::istringstream iss(line);
    BillDetail d;
    std::string marker;
    if (!(iss >> d.billId >> d.productAcronym >> d.unitPrice
             >> d.quantity >> d.lineDiscount >> marker))
        return std::nullopt;
    if (marker != "#") return std::nullopt;
    return d;
}

std::string BillDetailSerialiser::format(const BillDetail& d)
{
    std::ostringstream oss;
    oss << d.billId << ' ' << d.productAcronym << ' '
        << stringutils::money(d.unitPrice) << ' ' << d.quantity << ' '
        << stringutils::money(d.lineDiscount) << " #";
    return oss.str();
}