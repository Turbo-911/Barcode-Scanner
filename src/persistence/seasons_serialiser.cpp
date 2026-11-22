#include "persistence/seasons_serialiser.h"
#include "common/string_utils.h"

#include <sstream>

std::optional<Season> SeasonsSerialiser::parse(const std::string& line)
{
    std::istringstream iss(line);
    Season s;
    std::string active, marker;
    if (!(iss >> s.startDate >> s.endDate >> s.discountPercent >> active >> marker))
        return std::nullopt;
    if (marker != "#") return std::nullopt;
    s.isActive = (active == "true");
    return s;
}

std::string SeasonsSerialiser::format(const Season& s)
{
    std::ostringstream oss;
    oss << s.startDate << ' ' << s.endDate << ' '
        << stringutils::money(s.discountPercent) << ' '
        << (s.isActive ? "true" : "false") << " #";
    return oss.str();
}