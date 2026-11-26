#pragma once
#include <string>
#include <vector>

// payment_rules - the payment mode / merchant business rule (D-24,
// v38): which merchants make sense for each payment mode. Moved out
// of BarcodeStudioGui.cpp into domain/ so the rule lives in exactly
// one place and any front-end (GUI, console, tests) shares it.
// The rule itself is unchanged from the GUI's old local helper:
//   card   -> ABL, MCB, Meezan (the only merchants the service accepts)
//   wallet -> JazzCash, EasyPaisa, Raast
//   cash (or anything else) -> "-" (no merchant)
namespace payment_rules
{
std::vector<std::string> merchantOptionsFor(const std::string& mode);
}
