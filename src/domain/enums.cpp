#include "domain/enums.h"

namespace
{
    const std::vector<std::string> kOrderStatusNames = {
        "in-progress", "on-hold", "paid", "archived"
    };
    const std::vector<std::string> kPaymentModeNames = {
        "cash", "card", "wallet"
    };
    const std::vector<std::string> kUserRoleNames = {
        "Cashier", "Manager"
    };
}

namespace enums
{
    std::string toString(OrderStatus status)
    {
        return kOrderStatusNames[static_cast<std::size_t>(status)];
    }

    std::string toString(PaymentMode mode)
    {
        return kPaymentModeNames[static_cast<std::size_t>(mode)];
    }

    std::string toString(UserRole role)
    {
        return kUserRoleNames[static_cast<std::size_t>(role)];
    }

    bool fromString(const std::string& text, OrderStatus& out)
    {
        for (std::size_t i = 0; i < kOrderStatusNames.size(); ++i)
            if (kOrderStatusNames[i] == text)
            {
                out = static_cast<OrderStatus>(i);
                return true;
            }
        return false;
    }

    bool fromString(const std::string& text, PaymentMode& out)
    {
        for (std::size_t i = 0; i < kPaymentModeNames.size(); ++i)
            if (kPaymentModeNames[i] == text)
            {
                out = static_cast<PaymentMode>(i);
                return true;
            }
        return false;
    }

    bool fromString(const std::string& text, UserRole& out)
    {
        for (std::size_t i = 0; i < kUserRoleNames.size(); ++i)
            if (kUserRoleNames[i] == text)
            {
                out = static_cast<UserRole>(i);
                return true;
            }
        return false;
    }

    const std::vector<std::string>& orderStatusNames() { return kOrderStatusNames; }
    const std::vector<std::string>& paymentModeNames() { return kPaymentModeNames; }
    const std::vector<std::string>& userRoleNames() { return kUserRoleNames; }
}