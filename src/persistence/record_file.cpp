#include "persistence/record_file.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace
{
// Directory holding this process' executable (Windows only: on Linux
// the caller falls back to the working directory, per D-30). Used so
// Data/ is found next to the exe no matter where the app is launched
// from. Empty when it cannot be determined.
std::string exeDirectory()
{
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::string();
    const std::string full(buf, n);
    const std::size_t slash = full.find_last_of("\\/");
    if (slash == std::string::npos) return std::string();
    return full.substr(0, slash);
#else
    return std::string();
#endif
}

std::string lowerOf(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Case-insensitive fallback (D-19): if `path` itself does not exist,
// look for a file with the same name ignoring case in the same
// directory (so a spec-cased Product.txt is found when the code asks
// for Data/product.txt, and vice versa). Empty when there is no match.
std::string caseInsensitiveMatch(const std::string& path)
{
    std::error_code ec;
    std::filesystem::path p(path);
    std::filesystem::path dir = p.parent_path();
    if (dir.empty()) dir = ".";
    const std::string want = lowerOf(p.filename().string());
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) return std::string();
    for (const auto& entry : it)
    {
        if (lowerOf(entry.path().filename().string()) == want)
            return entry.path().string();
    }
    return std::string();
}
}

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
    // Atomic rewrite (D-02): write the whole file to <path>.tmp, then
    // rename it over the original, so a crash mid-write leaves either
    // the old file or the new file - never a truncated half-file.
    // std::filesystem::rename replaces an existing file (MinGW included).
    const std::string tmpPath = path + ".tmp";
    {
        std::ofstream out(tmpPath, std::ios::trunc);
        if (!out) return false;
        for (const std::string& line : lines) out << line << "\n";
        out.flush();
        if (!out) { std::error_code ec; std::filesystem::remove(tmpPath, ec); return false; }
    }
    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) { std::error_code ec2; std::filesystem::remove(tmpPath, ec2); return false; }
    return true;
}

bool RecordFile::writeBackup(const std::string& path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return false;
    std::filesystem::copy_file(path, path + ".bak",
        std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
}

std::string RecordFile::locate(const std::vector<std::string>& candidates)
{
    // D-30: Data/ is resolved relative to the EXE location first, then
    // the existing candidates (working directory, parent, project root).
    // D-19: each candidate also gets a case-insensitive fallback, so a
    // spec-cased Product.txt on disk is found when the code asks for
    // Data/product.txt.
    std::vector<std::string> expanded;
    const std::string exeDir = exeDirectory();
    for (const std::string& c : candidates)
    {
        if (!exeDir.empty()) expanded.push_back(exeDir + "/" + c);
        expanded.push_back(c);
    }
    for (const std::string& c : expanded)
        if (std::filesystem::exists(c)) return c;
    for (const std::string& c : expanded)
    {
        const std::string match = caseInsensitiveMatch(c);
        if (!match.empty()) return match;
    }
    return candidates.empty() ? std::string() : candidates.front();
}
