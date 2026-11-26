#pragma once
#include <optional>
#include <string>
#include <vector>

// JournalSerialiser - ONE journal entry <-> ONE Data/journal.txt line
// (SRP), the write-ahead journal for checkout / paid-order edit (v37,
// audit deficit D-02). The journal is written BEFORE any data file is
// touched, so a crash mid-checkout leaves a `pending` entry that
// startup recovery can roll forward from.
//
// Line format (space-separated, one record per line, ending `#` like
// every other data file):
//   orderId kind billId customerName customerContact date time
//   paymentMode paymentMerchant seasonalAmount specialAmount
//   amountReceived lines oldLines status #
//
//   orderId         8 digits; the entry id (reused as the order id)
//   kind            checkout | edit
//   billId          9 digits; the bill this attempt will write
//   customerName    '_' instead of spaces (stringutils::encode)
//   customerContact '-' when not given
//   date / time     the BILL date/time (checkout: today/now; edit: the
//                   original bill's date/time, so the seasonal discount
//                   is recomputed for the right day)
//   paymentMode     cash | card | wallet
//   paymentMerchant '-' for cash
//   seasonalAmount / specialAmount / amountReceived
//                   RUPEE amounts with 2 decimals (not percentages), so
//                   recovery rebuilds the exact bill even if the season
//                   changes between the crash and the restart
//   lines           productId:qty:stockBefore, comma-separated, no
//                   spaces. stockBefore is the product's stock at the
//                   moment the journal entry was written - recovery
//                   uses it to tell "stock not moved yet" from "stock
//                   already moved" and to make the stock fix absolute
//                   (setStock), which is what makes recovery safe to
//                   run twice.
//   oldLines        edit only: the lines being replaced, same
//                   productId:qty:stockBefore encoding (the stock
//                   snapshot is the same moment, before the edit moves
//                   any stock); '-' for a checkout
//   status          pending | done | aborted
//                   pending: written, checkout/edit not yet finished.
//                   done: the checkout/edit finished (all writes and
//                   all stock moves). aborted: the attempt returned a
//                   failure AFTER the journal write and its partial
//                   rows were rolled back - recovery must NOT resurrect
//                   it (a retry with new ids would otherwise be
//                   completed twice).
class JournalSerialiser
{
public:
    // One product line inside a journal entry: the quantity the
    // attempt moves, and the stock on hand when the entry was written.
    struct Line
    {
        int productId = 0;
        int quantity = 0;
        int stockBefore = 0;
    };
    struct Entry
    {
        std::string orderId;
        std::string kind;  // checkout | edit
        std::string billId;
        std::string customerName;
        std::string customerContact;
        std::string date;  // YYYY-MM-DD (the bill date)
        std::string time;  // HH:MM:SS (the bill time)
        std::string paymentMode;
        std::string paymentMerchant;
        double seasonalAmount = 0.0;
        double specialAmount = 0.0;
        double amountReceived = 0.0;
        std::vector<Line> lines;     // new lines (the sale / the edit)
        std::vector<Line> oldLines;  // edit only; empty for a checkout
        std::string status;  // pending | done | aborted
    };

    // Strict parse: wrong kind/status, a bad number, a bad line triple,
    // a missing `#`, or ANY token after `#` is malformed and returns
    // nullopt. A malformed line is a crash DURING the journal write
    // itself: the checkout never started, so recovery ignores it and
    // counts it as a dropped line instead of guessing.
    static std::optional<Entry> parse(const std::string& line);
    static std::string format(const Entry& entry);
    // The compact line-list encoding: "1001:2:40,1002:1:25", or "-"
    // for an empty list. Exposed so tests can hand-write journal lines.
    static std::string formatLines(const std::vector<Line>& lines);
    static std::optional<std::vector<Line>> parseLines(const std::string& token);
};
