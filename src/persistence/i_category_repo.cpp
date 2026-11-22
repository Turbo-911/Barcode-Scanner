#include "persistence/i_category_repo.h"
#include "persistence/category_serialiser.h"
#include "persistence/record_file.h"

CategoryFileRepository::CategoryFileRepository(std::string fileName)
    : m_path(RecordFile::locate({fileName, "../" + fileName, "Barcode-Scanner/" + fileName}))
{
}

std::vector<Category> CategoryFileRepository::loadAll()
{
    std::vector<Category> categories;
    for (const std::string& line : RecordFile::readAllLines(m_path))
        if (auto c = CategorySerialiser::parse(line)) categories.push_back(*c);
    return categories;
}

std::optional<Category> CategoryFileRepository::findById(const std::string& id)
{
    for (const Category& c : loadAll())
        if (c.id == id) return c;
    return std::nullopt;
}

bool CategoryFileRepository::saveAll(const std::vector<Category>& categories)
{
    std::vector<std::string> lines;
    for (const Category& c : categories) lines.push_back(CategorySerialiser::format(c));
    return RecordFile::writeAllLines(m_path, lines);
}