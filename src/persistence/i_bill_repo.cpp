#include "persistence/i_bill_repo.h"
#include "persistence/bill_serialiser.h"
#include "persistence/record_file.h"

BillFileRepository::BillFileRepository(std::string fileName)
    : m_path(RecordFile::locate({fileName, "../" + fileName, "Barcode-Scanner/" + fileName}))
{
}

bool BillFileRepository::appendBill(const Bill& bill)
{
    return RecordFile::appendLine(m_path, BillSerialiser::format(bill));
}

std::vector<Bill> BillFileRepository::loadAll()
{
    std::vector<Bill> bills;
    for (const std::string& line : RecordFile::readAllLines(m_path))
        if (auto b = BillSerialiser::parse(line)) bills.push_back(*b);
    return bills;
}

std::optional<Bill> BillFileRepository::findById(const std::string& id)
{
    for (const Bill& b : loadAll())
        if (b.id == id) return b;
    return std::nullopt;
}

int BillFileRepository::count()
{
    return static_cast<int>(loadAll().size());
}
bool BillFileRepository::saveAll(const std::vector<Bill>& bills)
{
    std::vector<std::string> lines;
    for (const Bill& b : bills) lines.push_back(BillSerialiser::format(b));
    return RecordFile::writeAllLines(m_path, lines);
}
