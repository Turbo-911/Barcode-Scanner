#pragma once
#include "domain/bill.h"
#include <string>

// IBillRepository - storage contract for bills (OCP / AoC).
// BillService depends on this interface, never on the file class.
class IBillRepository
{
public:
    virtual ~IBillRepository() = default;
    virtual bool append(const Bill& bill) = 0;
    virtual int count() = 0;
};

// BillFileRepository - IBillRepository backed by Data/bill.txt.
// Owns exactly one file; delegates IO to RecordFile and formatting
// to BillSerialiser (SRP). The member definitions live in
// i_bill_repo.cpp.
class BillFileRepository : public IBillRepository
{
public:
    explicit BillFileRepository(std::string fileName = "Data/bill.txt");
    bool append(const Bill& bill) override;
    int count() override;
private:
    std::string m_path;
};
