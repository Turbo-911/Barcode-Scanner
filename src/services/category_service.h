#pragma once
#include "persistence/category.h"
#include "persistence/i_category_repo.h"
#include <optional>
#include <string>
#include <vector>

class CategoryService
{
public:
    explicit CategoryService(ICategoryRepository& repo) : m_repo(repo) {}

    std::vector<Category> listAll() { return m_repo.loadAll(); }
    std::optional<Category> findById(const std::string& id) { return m_repo.findById(id); }

    std::string addCategory(const std::string& id, const std::string& name,
        const std::string& acronym);

    std::string updateCategory(const std::string& id, const std::string& name,
        const std::string& acronym);

    static std::string makeBarcode(const std::string& categoryId, int productId);

private:
    ICategoryRepository& m_repo;
};