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
    // Either 11 digits, or the dashed form 0300-1234567 (11 digits
    // with a dash after the fourth digit).
    std::string digits;
    for (const char c : value)
        if (c >= '0' && c <= '9') digits += c;
    if (digits.size() != 11) return false;
    if (value.size() == 11) return value == digits;
    return value.size() == 12 && value[4] == '-'
        && value.compare(0, 4, digits, 0, 4) == 0
        && value.compare(5, 7, digits, 4, 7) == 0;
}

}
