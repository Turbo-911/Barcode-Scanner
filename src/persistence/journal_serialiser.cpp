#include "persistence/journal_serialiser.h"
#include "common/date_time.h"
#include "common/string_utils.h"

#include <sstream>

namespace
{
// Strict number reads: the whole token must be a number, nothing else.
bool parseDoubleStrict(const std::string& token, double& out)
{
    try
    {
        std::size_t used = 0;
        out = std::stod(token, &used);
        return used == token.size();
    }
    catch (...)
    {
        return false;
    }
}

bool parseIntStrict(const std::string& token, int& out)
{
    try
    {
        std::size_t used = 0;
        out = std::stoi(token, &used);
        return used == token.size();
    }
    catch (...)
    {
        return false;
    }
}

bool validTime(const std::string& t)
{
    if (t.size() != 8 || t[2] != ':' || t[5] != ':') return false;
    int hh = 0, mm = 0, ss = 0;
    return parseIntStrict(t.substr(0, 2), hh) && parseIntStrict(t.substr(3, 2), mm)
        && parseIntStrict(t.substr(6, 2), ss)
        && hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59 && ss >= 0 && ss <= 59;
}
}

std::optional<std::vector<JournalSerialiser::Line>> JournalSerialiser::parseLines(
    const std::string& token)
{
    if (token == "-") return std::vector<Line>{};
    std::vector<Line> out;
    std::stringstream ss(token);
    std::string part;
    while (std::getline(ss, part, ','))
    {
        // One triple: productId:qty:stockBefore, all integers.
        std::stringstream ps(part);
        std::string idTok, qtyTok, stockTok;
        if (!std::getline(ps, idTok, ':') || !std::getline(ps, qtyTok, ':')
            || !std::getline(ps, stockTok, ':') || std::getline(ps, idTok, ':'))
            return std::nullopt;
        Line line;
        if (!parseIntStrict(idTok, line.productId)
            || !parseIntStrict(qtyTok, line.quantity)
            || !parseIntStrict(stockTok, line.stockBefore))
            return std::nullopt;
        if (line.productId <= 0 || line.quantity <= 0 || line.stockBefore < 0)
            return std::nullopt;
        out.push_back(line);
    }
    if (out.empty()) return std::nullopt;
    return out;
}

std::string JournalSerialiser::formatLines(const std::vector<Line>& lines)
{
    if (lines.empty()) return "-";
    std::ostringstream oss;
    bool first = true;
    for (const Line& line : lines)
    {
        if (!first) oss << ',';
        first = false;
        oss << line.productId << ':' << line.quantity << ':' << line.stockBefore;
    }
    return oss.str();
}

std::optional<JournalSerialiser::Entry> JournalSerialiser::parse(const std::string& line)
{
    std::istringstream iss(line);
    Entry e;
    std::string linesTok, oldLinesTok, marker, extra;
    std::string seasonalTok, specialTok, receivedTok;
    if (!(iss >> e.orderId >> e.kind >> e.billId >> e.customerName >> e.customerContact
             >> e.date >> e.time >> e.paymentMode >> e.paymentMerchant
             >> seasonalTok >> specialTok >> receivedTok
             >> linesTok >> oldLinesTok >> e.status >> marker))
        return std::nullopt;
    if (!parseDoubleStrict(seasonalTok, e.seasonalAmount)
        || !parseDoubleStrict(specialTok, e.specialAmount)
        || !parseDoubleStrict(receivedTok, e.amountReceived))
        return std::nullopt;
    if (marker != "#") return std::nullopt;
    if (iss >> extra) return std::nullopt;  // junk after '#' is malformed
    if (e.kind != "checkout" && e.kind != "edit") return std::nullopt;
    if (e.status != "pending" && e.status != "done" && e.status != "aborted")
        return std::nullopt;
    if (e.orderId.size() != 8 || e.billId.size() != 9) return std::nullopt;
    if (!datetime::isValidDate(e.date) || !validTime(e.time)) return std::nullopt;
    if (e.paymentMode != "cash" && e.paymentMode != "card" && e.paymentMode != "wallet")
        return std::nullopt;
    if (!(e.seasonalAmount >= 0.0) || !(e.specialAmount >= 0.0)
        || !(e.amountReceived >= 0.0))
        return std::nullopt;
    const auto lines = parseLines(linesTok);
    if (!lines) return std::nullopt;
    e.lines = *lines;
    const auto oldLines = parseLines(oldLinesTok);
    if (!oldLines) return std::nullopt;
    e.oldLines = *oldLines;
    // A checkout has no old lines; an edit must name the lines it
    // replaces (recovery needs them to reconcile the stock).
    if (e.kind == "checkout" && !e.oldLines.empty()) return std::nullopt;
    if (e.kind == "edit" && e.oldLines.empty()) return std::nullopt;
    e.customerName = stringutils::decode(e.customerName);
    e.customerContact = stringutils::decode(e.customerContact);
    e.paymentMerchant = stringutils::decode(e.paymentMerchant);
    return e;
}

std::string JournalSerialiser::format(const Entry& e)
{
    std::ostringstream oss;
    oss << e.orderId << ' ' << e.kind << ' ' << e.billId << ' '
        << stringutils::encode(e.customerName) << ' '
        << stringutils::encode(e.customerContact) << ' '
        << e.date << ' ' << e.time << ' ' << e.paymentMode << ' '
        << stringutils::encode(e.paymentMerchant) << ' '
        << stringutils::money(e.seasonalAmount) << ' '
        << stringutils::money(e.specialAmount) << ' '
        << stringutils::money(e.amountReceived) << ' '
        << formatLines(e.lines) << ' ' << formatLines(e.oldLines) << ' '
        << e.status << " #";
    return oss.str();
}
