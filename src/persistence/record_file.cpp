#include "persistence/record_file.h"

#include <filesystem>
#include <fstream>

std::vector<std::string> RecordFile::readAllLines(const std::string& path)
{
    std::vector<std::string> lines;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

bool RecordFile::appendLine(const std::string& path, const std::string& line)
{
    // Never glue onto a previous record: the shipped bill files once
    // ended without a trailing newline, so an append landed on the same
    // line and the line-based reader dropped BOTH records. If the file
    // already has content that does not end with a newline, end it first.
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && std::filesystem::file_size(path, ec) > 0)
    {
        std::ifstream probe(path, std::ios::binary | std::ios::ate);
        if (probe)
        {
            char last = '\0';
            probe.seekg(-1, std::ios::end);
            probe.get(last);
            if (last != '\n')
            {
                std::ofstream fix(path, std::ios::app | std::ios::binary);
                if (fix) fix << '\n';
            }
        }
    }
    std::ofstream out(path, std::ios::app);
    if (!out) return false;
    out << line << "\n";
    return static_cast<bool>(out);
}

bool RecordFile::writeAllLines(const std::string& path, const std::vector<std::string>& lines)
{
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    for (const std::string& line : lines) out << line << "\n";
    return static_cast<bool>(out);
}

std::string RecordFile::locate(const std::vector<std::string>& candidates)
{
    for (const std::string& c : candidates)
        if (std::filesystem::exists(c)) return c;
    return candidates.empty() ? std::string() : candidates.front();
}
