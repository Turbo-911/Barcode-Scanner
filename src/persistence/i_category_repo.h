#pragma once
#include "persistence/category.h"
#include <optional>
#include <string>
#include <vector>

class ICategoryRepository
{
public:
    virtual ~ICategoryRepository() = default;
    virtual std::vector<Category> loadAll() = 0;
    virtual std::optional<Category> findById(const std::string& id) = 0;
    virtual bool saveAll(const std::vector<Category>& categories) = 0;
    virtual std::string filePath() const = 0;
};

class CategoryFileRepository : public ICategoryRepository
{
public:
    explicit CategoryFileRepository(std::string fileName = "Data/PCategory.txt");
    std::vector<Category> loadAll() override;
    std::optional<Category> findById(const std::string& id) override;
    bool saveAll(const std::vector<Category>& categories) override;
    std::string filePath() const override { return m_path; }
private:
    std::string m_path;
};