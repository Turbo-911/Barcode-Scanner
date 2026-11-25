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
    if (!isTwoDigits(id) || id == "00") return "bad-id";  // D-20: "00" is not a category
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
    // D-20: category "00" would start the barcode with a zero, which the
    // team's own isValidCode rejects (first digit must be non-zero), so
    // it is refused here. The tail is the FULL product id - the old
    // productId % 10000 made 1001 and 11001 collide - so an id above
    // 9999 cannot fit the 6-digit scheme at all and is refused (empty
    // string) instead of silently colliding. Callers treat "" as
    // "bad-category". Whether the category exists in PCategory.txt is
    // checked by the callers that own the repository (ProductService
    // add/update), since this static helper has no repo to ask.
    if (!isTwoDigits(categoryId) || categoryId == "00") return std::string();
    if (productId < 0 || productId > 9999) return std::string();
    std::ostringstream oss;
    oss << categoryId;
    oss.width(4);
    oss.fill('0');
    oss << productId;
    return oss.str();
}