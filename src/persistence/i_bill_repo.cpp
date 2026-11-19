#include "persistence/i_bill_repo.h"
#include "persistence/bill_serialiser.h"
#include "persistence/record_file.h"

// BillFileRepository - member definitions (declared in i_bill_repo.h).

BillFileRepository::BillFileRepository(std::string fileName)
    : m_path(RecordFile::locate({fileName, "../" + fileName, "Barcode-Scanner/" + fileName})) {}
bool BillFileRepository::append(const Bill& bill)
{
    return RecordFile::appendLine(m_path, BillSerialiser::format(bill));
}
int BillFileRepository::count()
{
    int n = 0;
    std::string lastId;
    for (const std::string& line : RecordFile::readAllLines(m_path))
        if (auto b = BillSerialiser::parse(line))
            if (b->id != lastId) { ++n; lastId = b->id; }
    return n;
}
