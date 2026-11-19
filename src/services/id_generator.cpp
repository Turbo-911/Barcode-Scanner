#include "services/id_generator.h"
#include "common/date_time.h"

// The id schemes live here; the clock reading itself is
// common/date_time's job (SRP) - this file only assembles the
// digits, it never calls strftime or localtime directly.

std::string IdGenerator::newOrderId()
{
    return datetime::stamp("%H%M%S") + datetime::stamp("%d");  // 6 + 2 = 8 digits, ends with the day
}
std::string IdGenerator::newBillId()
{
    return "1" + datetime::stamp("%H%M%S") + datetime::stamp("%d");  // 1 + 6 + 2 = 9 digits
}
std::string IdGenerator::todayDate() { return datetime::todayDate(); }
std::string IdGenerator::nowTime() { return datetime::nowTime(); }
