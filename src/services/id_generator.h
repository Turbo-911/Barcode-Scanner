#pragma once
#include <string>

// IdGenerator - the ONLY place that knows how Order Ids and Bill
// Ids are formed (SRP, one place to change the scheme). The clock
// reading behind the dates/times is common/date_time's job.
//   Order Id: 8 digits  = HHMMSS (time) + DD (day of month, as required:
//             the last two digits indicate the date of the order)
//   Bill Id : 9 digits  = '1' + HHMMSS + DD
class IdGenerator
{
public:
    static std::string newOrderId();
    static std::string newBillId();
    static std::string todayDate();  // YYYY-MM-DD
    static std::string nowTime();    // HH:MM:SS
};
