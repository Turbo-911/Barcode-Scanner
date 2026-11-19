#include "common/string_utils.h"

#include <iomanip>
#include <sstream>

namespace stringutils
{
std::string encode(const std::string& human)
{
    std::string out = human;
    for (char& c : out) if (c == ' ') c = '_';
    return out.empty() ? std::string("-") : out;
}

std::string decode(const std::string& token)
{
    if (token == "-") return "";
    std::string out = token;
    for (char& c : out) if (c == '_') c = ' ';
    return out;
}

std::string money(double value)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

bool isValidPhone(const std::string& value)
{
    if (value.size() != 11) return false;
    for (const char c : value)
        if (c < '0' || c > '9') return false;
    return true;
}

}
