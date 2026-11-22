#pragma once
#include <string>

struct Season
{
    std::string startDate;
    std::string endDate;
    double discountPercent = 0.0;
    bool isActive = false;
};