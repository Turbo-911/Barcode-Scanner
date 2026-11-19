#pragma once
#include "domain/product.h"
#include <optional>
#include <string>
#include <vector>

// IProductRepository - storage contract for products (OCP / AoC).
// Services depend on THIS interface, never on a concrete file class,
// so tomorrow's database or web repository can be added as a new class
// without modifying any service or GUI code.
class IProductRepository
{
public:
    virtual ~IProductRepository() = default;
    virtual std::vector<Product> loadAll() = 0;
    virtual std::optional<Product> findByBarcode(const std::string& barcode) = 0;
    virtual std::optional<Product> findById(int id) = 0;
    virtual bool saveAll(const std::vector<Product>& products) = 0;  // e.g. after stock changes
    virtual std::string filePath() const = 0;
};

// ProductFileRepository - IProductRepository backed by Data/product.txt.
// Its single job: turn file lines into Products and back, using
// RecordFile for IO and ProductSerialiser for the line format (SRP).
// The member definitions live in i_product_repo.cpp.
class ProductFileRepository : public IProductRepository
{
public:
    explicit ProductFileRepository(std::string fileName = "Data/product.txt");
    std::vector<Product> loadAll() override;
    std::optional<Product> findByBarcode(const std::string& barcode) override;
    std::optional<Product> findById(int id) override;
    bool saveAll(const std::vector<Product>& products) override;
    std::string filePath() const override { return m_path; }

private:
    std::string m_path;
};
