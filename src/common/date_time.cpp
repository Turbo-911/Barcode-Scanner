#include "common/date_time.h"

#include <chrono>
#include <climits>
#include <cstdio>

namespace datetime
{
std::tm localNow()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm out{};
#ifdef _WIN32
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

std::string stamp(const char* format)
{
    char buf[32] = {};
    const std::tm tm = localNow();
    std::strftime(buf, sizeof(buf), format, &tm);
    return buf;
}

std::string todayDate() { return stamp("%Y-%m-%d"); }
std::string nowTime() { return stamp("%H:%M:%S"); }

bool isValidDate(const std::string& s)
{
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (i == 4 || i == 7) continue;
        if (s[i] < '0' || s[i] > '9') return false;
    }
    const int year = std::stoi(s.substr(0, 4));
    const int month = std::stoi(s.substr(5, 2));
    const int day = std::stoi(s.substr(8, 2));
    if (month < 1 || month > 12 || day < 1) return false;
    // Real month lengths: 2026-02-31 and 2026-04-31 are not dates.
    static const int daysInMonth[] =
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int last = daysInMonth[month - 1];
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (month == 2 && leap) last = 29;
    return day <= last;
}

int daysSinceEpoch(const std::string& s)
{
    if (!isValidDate(s)) return INT_MIN;
    const int y = std::stoi(s.substr(0, 4));
    const unsigned m = static_cast<unsigned>(std::stoi(s.substr(5, 2)));
    const unsigned d = static_cast<unsigned>(std::stoi(s.substr(8, 2)));
    // Howard Hinnant's days-from-civil: days since 1970-01-01.
    int yy = y - (m <= 2 ? 1 : 0);
    const int era = (yy >= 0 ? yy : yy - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(yy - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3u : 9u)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

int daysBetween(const std::string& from, const std::string& to)
{
    const int f = daysSinceEpoch(from);
    const int t = daysSinceEpoch(to);
    if (f == INT_MIN || t == INT_MIN) return INT_MIN;
    return t - f;
}
}
