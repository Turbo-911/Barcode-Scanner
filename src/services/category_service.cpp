#include "services/category_service.h"

#include <sstream>

namespace
{
    bool isTwoDigits(const std::string& s)
    {
        if (s.size() != 2) return false;
        for (const char c : s) if (c < '0' || c > '9') return false;
        return true;
    }
}

std::string CategoryService::addCategory(const std::string& id,
    const std::string& name, const std::string& acronym)
{
    if (!isTwoDigits(id)) return "bad-id";
    if (name.empty()) return "bad-name";
    if (acronym.empty()) return "bad-acronym";

    std::vector<Category> all = m_repo.loadAll();
    for (const Category& c : all)
        if (c.id == id) return "duplicate";

    Category c;
    c.id = id;
    c.name = name;
    c.acronym = acronym;
    all.push_back(c);
    return m_repo.saveAll(all) ? "" : "write-failed";
}

std::string CategoryService::updateCategory(const std::string& id,
    const std::string& name, const std::string& acronym)
{
    if (!isTwoDigits(id)) return "bad-id";
    if (name.empty()) return "bad-name";
    if (acronym.empty()) return "bad-acronym";

    std::vector<Category> all = m_repo.loadAll();
    for (Category& c : all)
    {
        if (c.id != id) continue;
        c.name = name;
        c.acronym = acronym;
        return m_repo.saveAll(all) ? "" : "write-failed";
    }
    return "not-found";
}

std::string CategoryService::makeBarcode(const std::string& categoryId, int productId)
{
    std::ostringstream oss;
    oss << categoryId;
    int tail = productId % 10000;
    if (tail < 0) tail = -tail;
    oss.width(4);
    oss.fill('0');
    oss << tail;
    return oss.str();
}