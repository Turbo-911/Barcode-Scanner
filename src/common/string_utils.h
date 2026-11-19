#pragma once
#include <string>

// string_utils - small shared text helpers for the space-separated
// data files (this was persistence/TextTokens.h before the move to
// the repository structure; same code, new home in common/).
// The file formats cannot contain spaces inside a field, so names are
// stored with '_' and shown with spaces again. One home for the rule
// keeps every serialiser consistent (reusability / DRY).
namespace stringutils
{
    // "Fresh Milk" -> "Fresh_Milk"; "" -> "-" (the empty-field marker).
    std::string encode(const std::string& human);
    // "Fresh_Milk" -> "Fresh Milk"; "-" -> "".
    std::string decode(const std::string& token);
    // Money with exactly 2 decimals, e.g. 180 -> "180.00".
    std::string money(double value);
    // Customer contact rule: exactly 11 digits, nothing else.
    bool isValidPhone(const std::string& value);
}
