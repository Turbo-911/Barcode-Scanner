#include "persistence/i_seasons_repo.h"
#include "persistence/seasons_serialiser.h"
#include "persistence/record_file.h"

SeasonsFileRepository::SeasonsFileRepository(std::string fileName)
    : m_path(RecordFile::locate({fileName, "../" + fileName, "Barcode-Scanner/" + fileName}))
{
}

std::vector<Season> SeasonsFileRepository::loadAll()
{
    std::vector<Season> seasons;
    m_droppedLines = 0;
    m_backupWritten = false;
    for (const std::string& line : RecordFile::readAllLines(m_path))
    {
        if (auto s = SeasonsSerialiser::parse(line)) seasons.push_back(*s);
        else ++m_droppedLines;  // D-03: counted, reported, never silent
    }
    return seasons;
}

bool SeasonsFileRepository::saveAll(const std::vector<Season>& seasons)
{
    if (m_droppedLines > 0 && !m_backupWritten)
    {
        RecordFile::writeBackup(m_path);
        m_backupWritten = true;
    }
    std::vector<std::string> lines;
    for (const Season& s : seasons) lines.push_back(SeasonsSerialiser::format(s));
    return RecordFile::writeAllLines(m_path, lines);
}