#pragma once
#include "persistence/category.h"
#include <optional>
#include <string>

class CategorySerialiser
{
public:
    static std::optional<Category> parse(const std::string& line);
    static std::string format(const Category& category);
};