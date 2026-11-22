#include "services/pdf_bill.h"
#include "common/string_utils.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
std::string esc(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 4);
    for (const char c : s)
    {
        if (c == '(') out += "\\(";
        else if (c == ')') out += "\\)";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

// One text line at (x, y) in PDF points (origin bottom-left).
void line(std::ostringstream& body, double x, double y,
    const std::string& text, int size)
{
    body << "BT /F1 " << size << " Tf "
         << x << " " << y << " Td (" << esc(text) << ") Tj ET\n";
}
}

std::string pdfbill::writePdf(const std::string& folder, const Bill& bill,
    const std::vector<BillDetail>& lines)
{
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const std::string path = folder + "/" + bill.id + ".pdf";

    std::ostringstream body;
    double y = 800.0;
    line(body, 50.0, y, "Chili Mili - Bill " + bill.id, 16); y -= 24.0;
    line(body, 50.0, y, "Customer: " + bill.customerName, 11); y -= 16.0;
    line(body, 50.0, y, "Order: " + bill.orderId + "  Date: " + bill.billDate +
        "  Time: " + bill.billTime, 11); y -= 24.0;
    line(body, 50.0, y, "Product       Qty   Unit (no tax)   Line Discount", 11); y -= 14.0;
    line(body, 50.0, y, "---------------------------------------------------------", 10); y -= 12.0;
    for (const BillDetail& d : lines)
    {
        std::ostringstream s;
        s << d.productAcronym << "   x" << d.quantity
          << "   " << stringutils::money(d.unitPrice)
          << "   -" << stringutils::money(d.lineDiscount);
        line(body, 50.0, y, s.str(), 10); y -= 14.0;
        if (y < 120.0) { y = 800.0; }
    }
    y -= 10.0;
    line(body, 50.0, y, "Subtotal (no tax): " + stringutils::money(bill.totalWithoutTax), 11); y -= 14.0;
    line(body, 50.0, y, "Tax:               " + stringutils::money(bill.totalTax), 11); y -= 14.0;
    line(body, 50.0, y, "Discount:          -" + stringutils::money(bill.totalDiscount), 11); y -= 14.0;
    line(body, 50.0, y, "Seasonal Discount: -" + stringutils::money(bill.seasonalDiscount), 11); y -= 14.0;
    line(body, 50.0, y, "Special Discount:  -" + stringutils::money(bill.specialDiscount), 11); y -= 14.0;
    line(body, 50.0, y, "Payable:           " + stringutils::money(bill.totalPayable), 13); y -= 18.0;
    line(body, 50.0, y, "Amount Received:   " + stringutils::money(bill.amountReceived), 11); y -= 14.0;
    line(body, 50.0, y, "Balance Returned:  " + stringutils::money(bill.balanceReturned), 11); y -= 14.0;
    line(body, 50.0, y, "Status: " + bill.paymentStatus, 11);

    const std::string content = body.str();

    std::vector<std::string> objects;
    objects.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    objects.push_back("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>");
    {
        std::ostringstream s;
        s << "<< /Length " << content.size() << " >>\nstream\n"
          << content << "endstream";
        objects.push_back(s.str());
    }
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");

    std::ofstream out(path, std::ios::binary);
    if (!out) return "";

    std::vector<std::size_t> offsets;
    std::ostringstream head;
    head << "%PDF-1.4\n";
    for (std::size_t i = 0; i < objects.size(); ++i)
    {
        offsets.push_back(head.str().size());
        head << (i + 1) << " 0 obj\n" << objects[i] << "\nendobj\n";
    }
    const std::size_t xref = head.str().size();
    std::ostringstream tail;
    tail << "xref\n0 " << (objects.size() + 1) << "\n";
    tail << "0000000000 65535 f \n";
    for (const std::size_t off : offsets)
    {
        tail.width(10); tail.fill('0'); tail << off;
        tail << " 00000 n \n";
    }
    tail << "trailer << /Size " << (objects.size() + 1)
         << " /Root 1 0 R >>\nstartxref\n" << xref << "\n%%EOF\n";

    out << head.str() << tail.str();
    return static_cast<bool>(out) ? path : std::string();
}