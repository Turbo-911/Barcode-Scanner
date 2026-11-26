#pragma once
#include <string>

// audit_log - v38 (D-02a): the append-only manager-approval log,
// Data/audit.txt. One line per approval, in the same space-separated
// '#' -terminated record style as the other Data files:
//
//   <orderId> <date> <time> #
//
// e.g. "09263117 2026-11-26 12:34:56 #". The file is created on the
// first approval (RecordFile::appendLine creates a missing file).
// The GUI owns the PIN: batch B wires this into the PIN-accepted
// handler (BarcodeStudioGui.cpp, the PinPurpose::Approve branch that
// sets m_oeManagerApproved = true) with exactly this call:
//
//   audit_log::logManagerApproval(m_oeLoadedId);
//
// One call per approval, at approval time (not at save time - the
// approval is the audited event, and a save may never follow).
// Returns false only when the append itself fails.
namespace audit_log
{
    // Log an approval for `orderId`, dated/timed now (same
    // IdGenerator wall clock as order/bill dates).
    bool logManagerApproval(const std::string& orderId);
    // Same, with an explicit date (YYYY-MM-DD) and time (HH:MM:SS) -
    // for tests and for callers that already hold the approval moment.
    bool logManagerApproval(const std::string& orderId,
        const std::string& date, const std::string& time);
    // The resolved path of Data/audit.txt (via RecordFile::locate).
    std::string filePath();
}
