#include "domain/payment_rules.h"

namespace payment_rules
{
std::vector<std::string> merchantOptionsFor(const std::string& mode)
{
    if (mode == "card") return {"ABL", "MCB", "Meezan"};
    if (mode == "wallet") return {"JazzCash", "EasyPaisa", "Raast"};
    return {"-"};  // cash has no merchant
}
}
