#include "persistence/journal_repo.h"
#include "persistence/record_file.h"

// JournalFileRepository - member definitions (declared in
// journal_repo.h). Same shape as the other file repositories.

JournalFileRepository::JournalFileRepository(std::string fileName)
    : m_path(RecordFile::locate({fileName, "../" + fileName, "Barcode-Scanner/" + fileName})) {}

bool JournalFileRepository::append(const JournalSerialiser::Entry& entry)
{
    return RecordFile::appendLine(m_path, JournalSerialiser::format(entry));
}

std::vector<JournalSerialiser::Entry> JournalFileRepository::loadAll()
{
    std::vector<JournalSerialiser::Entry> entries;
    m_droppedLines = 0;
    m_backupWritten = false;
    for (const std::string& line : RecordFile::readAllLines(m_path))
    {
        if (auto e = JournalSerialiser::parse(line)) entries.push_back(*e);
        else ++m_droppedLines;  // D-03: counted, reported, never silent
    }
    return entries;
}

bool JournalFileRepository::saveAll(const std::vector<JournalSerialiser::Entry>& entries)
{
    if (m_droppedLines > 0 && !m_backupWritten)
    {
        RecordFile::writeBackup(m_path);
        m_backupWritten = true;
    }
    std::vector<std::string> lines;
    for (const JournalSerialiser::Entry& e : entries)
        lines.push_back(JournalSerialiser::format(e));
    return RecordFile::writeAllLines(m_path, lines);
}

bool JournalFileRepository::markStatus(const std::string& orderId,
    const std::string& kind, const std::string& status)
{
    std::vector<JournalSerialiser::Entry> entries = loadAll();
    for (JournalSerialiser::Entry& e : entries)
    {
        if (e.orderId == orderId && e.kind == kind && e.status == "pending")
        {
            e.status = status;
            return saveAll(entries);
        }
    }
    return false;
}
