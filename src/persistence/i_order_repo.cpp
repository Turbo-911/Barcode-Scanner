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

std::vector<Order> OrderFileRepository::loadAll()
{
    std::vector<Order> orders;
    m_droppedLines = 0;
    m_backupWritten = false;
    for (const std::string& line : RecordFile::readAllLines(m_path))
    {
        if (auto o = OrderSerialiser::parse(line)) orders.push_back(*o);
        else ++m_droppedLines;  // D-03: counted, reported, never silent
    }
    return orders;
}
bool OrderFileRepository::saveAll(const std::vector<Order>& orders)
{
    if (m_droppedLines > 0 && !m_backupWritten)
    {
        RecordFile::writeBackup(m_path);
        m_backupWritten = true;
    }
    std::vector<std::string> lines;
    for (const Order& o : orders) lines.push_back(OrderSerialiser::format(o));
    return RecordFile::writeAllLines(m_path, lines);
}
std::vector<OrderDetail> OrderDetailFileRepository::loadAll()
{
    std::vector<OrderDetail> details;
    m_droppedLines = 0;
    m_backupWritten = false;
    for (const std::string& line : RecordFile::readAllLines(m_path))
    {
        if (auto d = OrderDetailSerialiser::parse(line)) details.push_back(*d);
        else ++m_droppedLines;  // D-03: counted, reported, never silent
    }
    return details;
}
std::vector<OrderDetail> OrderDetailFileRepository::loadForOrder(const std::string& orderId)
{
    std::vector<OrderDetail> out;
    for (const OrderDetail& d : loadAll())
        if (d.orderId == orderId) out.push_back(d);
    return out;
}
bool OrderDetailFileRepository::saveAll(const std::vector<OrderDetail>& details)
{
    if (m_droppedLines > 0 && !m_backupWritten)
    {
        RecordFile::writeBackup(m_path);
        m_backupWritten = true;
    }
    std::vector<std::string> lines;
    for (const OrderDetail& d : details) lines.push_back(OrderDetailSerialiser::format(d));
    return RecordFile::writeAllLines(m_path, lines);
}
