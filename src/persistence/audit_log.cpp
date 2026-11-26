#include "persistence/audit_log.h"
#include "persistence/record_file.h"
#include "services/id_generator.h"

namespace audit_log
{
std::string filePath()
{
    return RecordFile::locate({"Data/audit.txt", "../Data/audit.txt",
        "Barcode-Scanner/Data/audit.txt"});
}

bool logManagerApproval(const std::string& orderId)
{
    return logManagerApproval(orderId, IdGenerator::todayDate(),
        IdGenerator::nowTime());
}

bool logManagerApproval(const std::string& orderId,
    const std::string& date, const std::string& time)
{
    if (orderId.empty() || date.empty() || time.empty()) return false;
    return RecordFile::appendLine(filePath(),
        orderId + " " + date + " " + time + " #");
}
}
