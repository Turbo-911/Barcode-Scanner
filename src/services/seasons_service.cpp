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
    // D-17: a 100% season is a free shop - the cap is strictly below 100.
    if (!(discountPercent >= 0.0) || discountPercent >= 100.0) return "bad-percent";

    std::vector<Season> all = m_repo.loadAll();
    if (isActive)
    {
        // D-17: an ACTIVE season must not overlap another ACTIVE season -
        // overlapping seasons shadow each other by file order, so the
        // discount a bill gets would depend on row order. Inactive
        // seasons may overlap; they affect nothing until activated.
        for (const Season& s : all)
        {
            if (!s.isActive) continue;
            if (startDate <= s.endDate && s.startDate <= endDate) return "overlap";
        }
    }

    Season s;
    s.startDate = startDate;
    s.endDate = endDate;
    s.discountPercent = discountPercent;
    s.isActive = isActive;

    all.push_back(s);
    return m_repo.saveAll(all) ? "" : "write-failed";
}

std::string SeasonsService::setActive(const std::string& startDate, bool isActive)
{
    // Kept for compatibility: keyed by start date only, so it toggles
    // the FIRST season with that start date. Prefer the (start, end)
    // overload below, which identifies the season exactly (D-17).
    std::vector<Season> all = m_repo.loadAll();
    for (Season& s : all)
    {
        if (s.startDate != startDate) continue;
        s.isActive = isActive;
        return m_repo.saveAll(all) ? "" : "write-failed";
    }
    return "not-found";
}

std::string SeasonsService::setActive(const std::string& startDate,
    const std::string& endDate, bool isActive)
{
    // D-17: keyed by the (start, end) date pair - two seasons that share
    // a start date are now toggled independently.
    std::vector<Season> all = m_repo.loadAll();
    for (Season& s : all)
    {
        if (s.startDate != startDate || s.endDate != endDate) continue;
        s.isActive = isActive;
        return m_repo.saveAll(all) ? "" : "write-failed";
    }
    return "not-found";
}