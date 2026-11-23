#include "services/seasons_service.h"
#include "common/date_time.h"

namespace
{
    bool isDate(const std::string& s) { return datetime::isValidDate(s); }
}

double SeasonsService::activeDiscountPercentOn(const std::string& date)
{
    for (const Season& s : m_repo.loadAll())
    {
        if (!s.isActive) continue;
        if (date >= s.startDate && date <= s.endDate)
            return s.discountPercent;
    }
    return 0.0;
}

std::string SeasonsService::addSeason(const std::string& startDate,
    const std::string& endDate, double discountPercent, bool isActive)
{
    if (!isDate(startDate) || !isDate(endDate)) return "bad-date";
    if (startDate > endDate) return "bad-range";
    if (discountPercent < 0.0 || discountPercent > 100.0) return "bad-percent";

    Season s;
    s.startDate = startDate;
    s.endDate = endDate;
    s.discountPercent = discountPercent;
    s.isActive = isActive;

    std::vector<Season> all = m_repo.loadAll();
    all.push_back(s);
    return m_repo.saveAll(all) ? "" : "write-failed";
}

std::string SeasonsService::setActive(const std::string& startDate, bool isActive)
{
    std::vector<Season> all = m_repo.loadAll();
    for (Season& s : all)
    {
        if (s.startDate != startDate) continue;
        s.isActive = isActive;
        return m_repo.saveAll(all) ? "" : "write-failed";
    }
    return "not-found";
}