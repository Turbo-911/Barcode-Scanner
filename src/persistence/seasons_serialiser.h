#pragma once
#include "persistence/seasons.h"
#include <optional>
#include <string>

class SeasonsSerialiser
{
public:
    static std::optional<Season> parse(const std::string& line);
    static std::string format(const Season& season);
};