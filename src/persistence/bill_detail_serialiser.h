#pragma once
#include "domain/bill.h"
#include <optional>
#include <string>

class BillDetailSerialiser
{
public:
    static std::optional<BillDetail> parse(const std::string& line);
    static std::string format(const BillDetail& detail);
};