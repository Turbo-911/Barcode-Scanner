#include "persistence/product_serialiser.h"
#include "common/string_utils.h"

#include <sstream>

std::optional<Product> ProductSerialiser::parse(const std::string& line)
{
    std::istringstream iss(line);
    Product p;
    std::string marker;
    if (!(iss >> p.id >> p.barcode >> p.acronym >> p.name >> p.unit
             >> p.unitPrice >> p.taxPerUnit >> p.discountPerUnit
             >> p.availableQuantity >> p.barcodeFile >> marker))
        return std::nullopt;
    if (marker != "#") return std::nullopt;
    p.name = stringutils::decode(p.name);
    return p;
}

std::string ProductSerialiser::format(const Product& p)
{
    std::ostringstream oss;
    oss << p.id << ' ' << p.barcode << ' ' << p.acronym << ' '
        << stringutils::encode(p.name) << ' ' << p.unit << ' '
        << stringutils::money(p.unitPrice) << ' '
        << stringutils::money(p.taxPerUnit) << ' '
        << stringutils::money(p.discountPerUnit) << ' '
        << p.availableQuantity << ' ' << p.barcodeFile << " #";
    return oss.str();
}
