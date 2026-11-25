#include "persistence/i_bill_detail_repo.h"
#include "persistence/bill_detail_serialiser.h"
#include "persistence/record_file.h"

BillDetailFileRepository::BillDetailFileRepository(std::string fileName)
    : m_path(RecordFile::locate({fileName, "../" + fileName, "Barcode-Scanner/" + fileName}))
{
}

bool BillDetailFileRepository::append(const BillDetail& detail)
{
    return RecordFile::appendLine(m_path, BillDetailSerialiser::format(detail));
}

std::vector<BillDetail> BillDetailFileRepository::loadAll()
{
    std::vector<BillDetail> details;
    m_droppedLines = 0;
    m_backupWritten = false;
    for (const std::string& line : RecordFile::readAllLines(m_path))
    {
        if (auto d = BillDetailSerialiser::parse(line)) details.push_back(*d);
        else ++m_droppedLines;  // D-03: counted, reported, never silent
    }
    return details;
}

std::vector<BillDetail> BillDetailFileRepository::loadForBill(const std::string& billId)
{
    std::vector<BillDetail> out;
    for (const BillDetail& d : loadAll())
        if (d.billId == billId) out.push_back(d);
    return out;
}
bool BillDetailFileRepository::saveAll(const std::vector<BillDetail>& details)
{
    if (m_droppedLines > 0 && !m_backupWritten)
    {
        RecordFile::writeBackup(m_path);
        m_backupWritten = true;
    }
    std::vector<std::string> lines;
    for (const BillDetail& d : details) lines.push_back(BillDetailSerialiser::format(d));
    return RecordFile::writeAllLines(m_path, lines);
}
