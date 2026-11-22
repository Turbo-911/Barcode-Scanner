#pragma once
#include "domain/bill.h"
#include "domain/order.h"
#include <string>
#include <vector>


namespace pdfbill
{
    std::string writePdf(const std::string& folder, const Bill& bill,
        const std::vector<BillDetail>& lines);
}