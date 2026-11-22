#include "persistence/category_serialiser.h"
#include "common/string_utils.h"

#include <sstream>

std::optional<Category> CategorySerialiser::parse(const std::string& line)
{
    std::istringstream iss(line);
    Category c;
    std::string marker;
    if (!(iss >> c.id >> c.name >> c.acronym >> marker))
        return std::nullopt;
    if (marker != "#") return std::nullopt;
    c.name = stringutils::decode(c.name);
    return c;
}

std::string CategorySerialiser::format(const Category& c)
{
    std::ostringstream oss;
    oss << c.id << ' ' << stringutils::encode(c.name) << ' '
        << c.acronym << " #";
    return oss.str();
}