#pragma once
#include "domain/bill.h"
#include <optional>
#include <string>
#include <vector>

// IBillRepository - storage contract for bills (OCP / AoC).
// One Bill = one row in Data/bill.txt; the bill's products live in
// Data/BillDetail.txt (see IBillDetailRepository). BillService depends
// on this interface, never on the file class.
class IBillRepository
{
public:
    virtual ~IBillRepository() = default;
    virtual bool appendBill(const Bill& bill) = 0;
    virtual std::vector<Bill> loadAll() = 0;
    virtual bool saveAll(const std::vector<Bill>& bills) = 0;
    virtual std::optional<Bill> findById(const std::string& id) = 0;
    virtual int count() = 0;
    virtual std::string filePath() const = 0;
};

// BillFileRepository - IBillRepository backed by Data/bill.txt.
// Owns exactly one file; delegates IO to RecordFile and formatting
// to BillSerialiser (SRP).
class BillFileRepository : public IBillRepository
{
public:
    explicit BillFileRepository(std::string fileName = "Data/bill.txt");
    bool appendBill(const Bill& bill) override;
    std::vector<Bill> loadAll() override;
    bool saveAll(const std::vector<Bill>& bills) override;
    std::optional<Bill> findById(const std::string& id) override;
    int count() override;
    std::string filePath() const override { return m_path; }
private:
    std::string m_path;
};