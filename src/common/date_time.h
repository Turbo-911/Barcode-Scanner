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
}
