#include "persistence/i_order_repo.h"
#include "persistence/order_detail_serialiser.h"
#include "persistence/order_serialiser.h"
#include "persistence/record_file.h"

// OrderFileRepository / OrderDetailFileRepository - member definitions
// (declared in i_order_repo.h).

OrderFileRepository::OrderFileRepository(std::string fileName)
    : m_path(RecordFile::locate({fileName, "../" + fileName, "Barcode-Scanner/" + fileName})) {}
bool OrderFileRepository::append(const Order& order)
{
    return RecordFile::appendLine(m_path, OrderSerialiser::format(order));
}
int OrderFileRepository::count()
{
    return static_cast<int>(RecordFile::readAllLines(m_path).size());
}

OrderDetailFileRepository::OrderDetailFileRepository(std::string fileName)
    : m_path(RecordFile::locate({fileName, "../" + fileName, "Barcode-Scanner/" + fileName})) {}
bool OrderDetailFileRepository::append(const OrderDetail& detail)
{
    return RecordFile::appendLine(m_path, OrderDetailSerialiser::format(detail));
}
