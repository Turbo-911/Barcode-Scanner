#pragma once
#include <string>
#include <vector>

// enums - the fixed value sets of the POS domain, in ONE place (SRP):
// the order statuses and payment modes allowed by the requirements.
// The data files store these values as plain text (e.g. "on-hold"), so
// the enums convert to and from exactly those strings; services and
// the GUI take their lists from here instead of keeping private
// copies that could drift apart (reusability / DRY).
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

namespace enums
{
    // The file/GUI text of each value, e.g. toString(OrderStatus::OnHold) == "on-hold".
    std::string toString(OrderStatus status);
    std::string toString(PaymentMode mode);

    // Parse file/GUI text back. Returns false when the text is not a known value.
    bool fromString(const std::string& text, OrderStatus& out);
    bool fromString(const std::string& text, PaymentMode& out);

    // All values as file/GUI text, in display order - used for the
    // GUI's cycle buttons and for validation.
    const std::vector<std::string>& orderStatusNames();
    const std::vector<std::string>& paymentModeNames();
}
