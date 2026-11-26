#pragma once
#include "persistence/journal_serialiser.h"
#include <string>
#include <vector>

// JournalFileRepository - the write-ahead journal (v37, D-02) backed
// by Data/journal.txt. One small interface-per-file class like the
// other file repositories (SRP): it owns exactly this file and
// delegates IO to RecordFile and formatting to JournalSerialiser.
// The journal append goes through RecordFile::appendLine - the same
// atomic append path as every other data file (it never glues onto a
// previous record that lacks a trailing newline).
class JournalFileRepository
{
public:
    explicit JournalFileRepository(std::string fileName = "Data/journal.txt");
    bool append(const JournalSerialiser::Entry& entry);
    std::vector<JournalSerialiser::Entry> loadAll();
    bool saveAll(const std::vector<JournalSerialiser::Entry>& entries);
    // Flip one entry's status (pending -> done / aborted) and rewrite
    // the file atomically. Matches the FIRST entry with this order id
    // and kind that is still pending. Returns false when no such entry
    // exists or the rewrite fails - the caller treats that as
    // best-effort: the entry stays pending and the next startup
    // recovery re-applies it idempotently.
    bool markStatus(const std::string& orderId, const std::string& kind,
        const std::string& status);
    bool markDone(const std::string& orderId, const std::string& kind)
    {
        return markStatus(orderId, kind, "done");
    }
    bool markAborted(const std::string& orderId, const std::string& kind)
    {
        return markStatus(orderId, kind, "aborted");
    }
    std::string filePath() const { return m_path; }
    // Lines lost on the last loadAll (D-03): a malformed/partial line
    // is a crash DURING the journal write itself - the checkout never
    // started, so it is counted here, ignored by recovery, and never
    // silently dropped. 0 when the last load was clean.
    int droppedLines() const { return m_droppedLines; }

private:
    std::string m_path;
    int m_droppedLines = 0;
    bool m_backupWritten = false;
};
