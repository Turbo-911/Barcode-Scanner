#pragma once
#include "domain/order.h"
#include <string>

// Storage contracts for orders and order details (OCP / AoC).
// One small interface per file, so each can evolve independently (SRP).
// (The bill contract has its own header: i_bill_repo.h.)
class IOrderRepository
{
public:
    virtual ~IOrderRepository() = default;
    virtual bool append(const Order& order) = 0;
    virtual int count() = 0;
};

class IOrderDetailRepository
{
public:
    virtual ~IOrderDetailRepository() = default;
    virtual bool append(const OrderDetail& detail) = 0;
};

// File-backed repositories for Data/order.txt and Data/orderDetail.txt.
// Each class owns exactly one file and delegates IO to RecordFile and
// formatting to its serialiser (SRP). The member definitions live in
// i_order_repo.cpp.
class OrderFileRepository : public IOrderRepository
{
public:
    explicit OrderFileRepository(std::string fileName = "Data/order.txt");
    bool append(const Order& order) override;
    int count() override;
private:
    std::string m_path;
};

class OrderDetailFileRepository : public IOrderDetailRepository
{
public:
    explicit OrderDetailFileRepository(std::string fileName = "Data/orderDetail.txt");
    bool append(const OrderDetail& detail) override;
private:
    std::string m_path;
};
