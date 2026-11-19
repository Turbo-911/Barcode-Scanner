#include "common/date_time.h"

#include <chrono>
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
}
