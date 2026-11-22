#pragma once
#include "domain/product.h"
#include <optional>
#include <string>

// ProductSerialiser - converts ONE Product <-> ONE Data/product.txt line.
// It knows the field order and nothing else (SRP):
//   id barcode acronym name unit price tax discount qty barcodeFile categoryId #
// Names are written with '_' for spaces (see TextTokens).
class ProductSerialiser
{
public:
    static std::optional<Product> parse(const std::string& line);
    static std::string format(const Product& product);
};