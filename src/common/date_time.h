#pragma once
#include <ctime>
#include <string>

// date_time - the ONLY place that reads the system clock (SRP).
// IdGenerator builds order/bill ids from these stamps; nothing else
// in the project calls strftime or localtime directly.
namespace datetime
{
    // Local time broken down (replaces localtime_s / localtime_r calls).
    std::tm localNow();

    // Format the current local time, e.g. stamp("%Y-%m-%d") -> "2026-11-18".
    std::string stamp(const char* format);

    std::string todayDate();  // YYYY-MM-DD

    // Strict YYYY-MM-DD with real month (1-12) and day (1-31) ranges.
    bool isValidDate(const std::string& s);
    std::string nowTime();    // HH:MM:SS

    // Real calendar maths (Howard Hinnant days-from-civil, no library):
    // days since 1970-01-01 for a YYYY-MM-DD date, or INT_MIN when the
    // date is not valid. THE one place date differences are computed -
    // every service uses this instead of its own y*372+m*31+d maths.
    int daysSinceEpoch(const std::string& date);
    // Whole days from `from` to `to` (to - from); INT_MIN if either
    // date is not valid.
    int daysBetween(const std::string& from, const std::string& to);
}
