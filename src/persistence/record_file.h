#pragma once
#include <string>
#include <vector>

// RecordFile — the ONLY class that touches raw text files for the
// Phase 2 data files. Repositories compose it instead of each doing
// their own file IO (SRP + reusability).
class RecordFile
{
public:
    static std::vector<std::string> readAllLines(const std::string& path);
    static bool appendLine(const std::string& path, const std::string& line);
    static bool writeAllLines(const std::string& path, const std::vector<std::string>& lines);
    // Copy the current file to <path>.bak (overwriting any old .bak),
    // so a save that drops lines can be undone. Returns false when
    // there is nothing to back up or the copy fails.
    static bool writeBackup(const std::string& path);
    // Returns the first candidate that exists, or the first candidate
    // (the default location) when none exists yet.
    static std::string locate(const std::vector<std::string>& candidates);
};
