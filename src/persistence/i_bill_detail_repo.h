#pragma once
#include "domain/bill.h"
#include <string>
#include <vector>

class IBillDetailRepository
{
public:
    virtual ~IBillDetailRepository() = default;
    virtual bool append(const BillDetail& detail) = 0;
    virtual bool saveAll(const std::vector<BillDetail>& details) = 0;
    virtual std::vector<BillDetail> loadForBill(const std::string& billId) = 0;
    virtual std::vector<BillDetail> loadAll() = 0;
    virtual std::string filePath() const = 0;
    // Lines lost on the last loadAll (D-03). 0 when the last load was clean.
    virtual int droppedLines() const { return 0; }
};

class BillDetailFileRepository : public IBillDetailRepository
{
public:
    explicit BillDetailFileRepository(std::string fileName = "Data/BillDetail.txt");
    bool append(const BillDetail& detail) override;
    bool saveAll(const std::vector<BillDetail>& details) override;
    std::vector<BillDetail> loadForBill(const std::string& billId) override;
    std::vector<BillDetail> loadAll() override;
    std::string filePath() const override { return m_path; }
    int droppedLines() const override { return m_droppedLines; }
private:
    std::string m_path;
    int m_droppedLines = 0;
    bool m_backupWritten = false;
};