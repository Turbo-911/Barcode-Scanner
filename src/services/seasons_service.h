#pragma once
#include "persistence/i_seasons_repo.h"
#include "persistence/seasons.h"
#include <string>
#include <vector>

class SeasonsService
{
public:
    explicit SeasonsService(ISeasonsRepository& repo) : m_repo(repo) {}

    std::vector<Season> listAll() { return m_repo.loadAll(); }

    double activeDiscountPercentOn(const std::string& date);

    std::string addSeason(const std::string& startDate, const std::string& endDate,
        double discountPercent, bool isActive);

    std::string setActive(const std::string& startDate, bool isActive);

    // D-17: toggle by the (start, end) date pair - the exact season,
    // even when two seasons share a start date. New callers use this.
    std::string setActive(const std::string& startDate,
        const std::string& endDate, bool isActive);

private:
    ISeasonsRepository& m_repo;
};