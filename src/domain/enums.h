#pragma once
#include <string>
#include <vector>

// enums - the fixed value sets of the POS domain, in ONE place (SRP):
// the order statuses, payment modes and user roles allowed by the
// requirements. The data files store these values as plain text (e.g.
// "on-hold"), so the enums convert to and from exactly those strings;
// services and the GUI take their lists from here instead of keeping
// private copies that could drift apart (reusability / DRY).
enum class OrderStatus
{
    InProgress,
    OnHold,
    Paid,
    Archived
};

enum class PaymentMode
{
    Cash,
    Card,
    Wallet
};

// Who is using the till. Cashiers record orders; managers additionally
// configure categories, edit products, view/update stock, manage seasons,
// view reports and approve edits of paid orders (requirement 12).
enum class UserRole
{
    Cashier,
    Manager
};

namespace enums
{
    // The file/GUI text of each value, e.g. toString(OrderStatus::OnHold) == "on-hold".
    std::string toString(OrderStatus status);
    std::string toString(PaymentMode mode);
    std::string toString(UserRole role);

    // Parse file/GUI text back. Returns false when the text is not a known value.
    bool fromString(const std::string& text, OrderStatus& out);
    bool fromString(const std::string& text, PaymentMode& out);
    bool fromString(const std::string& text, UserRole& out);

    // All values as file/GUI text, in display order - used for the
    // GUI's cycle buttons and for validation.
    const std::vector<std::string>& orderStatusNames();
    const std::vector<std::string>& paymentModeNames();
    const std::vector<std::string>& userRoleNames();
}