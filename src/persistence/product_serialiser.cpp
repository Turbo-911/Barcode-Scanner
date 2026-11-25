#include "persistence/product_serialiser.h"
#include "common/string_utils.h"

#include <sstream>

std::optional<Product> ProductSerialiser::parse(const std::string& line)
{
    // D-19: accept BOTH line shapes - the 11-field Phase 2 line (no
    // categoryId, ends after barcodeFile) and the 12-field Phase 3 line
    // (categoryId before the '#'). A Phase 2 line derives its category
    // from the first 2 digits of its barcode, as the spec requires.
    std::istringstream iss(line);
    Product p;
    std::string marker;
    if (!(iss >> p.id >> p.barcode >> p.acronym >> p.name >> p.unit
             >> p.unitPrice >> p.taxPerUnit >> p.discountPerUnit
             >> p.availableQuantity >> p.barcodeFile >> marker))
        return std::nullopt;
    if (marker == "#")
    {
        // Phase 2 line: no categoryId stored; derive it from the barcode.
        p.categoryId = p.barcode.size() >= 2 ? p.barcode.substr(0, 2) : "";
    }
    else
    {
        // Phase 3 line: the token just read is the categoryId, and a
        // '#' marker must follow it.
        p.categoryId = marker;
        if (!(iss >> marker) || marker != "#") return std::nullopt;
    }
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
        << p.availableQuantity << ' ' << p.barcodeFile << ' '
        << p.categoryId << " #";
    return oss.str();
}