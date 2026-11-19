#pragma once
#include "domain/product.h"
#include "persistence/i_product_repo.h"
#include <optional>
#include <string>
#include <vector>

// ProductService — everything the app DOES with products (SRP):
// lookups for the description feature, the stock rule, and stock
// updates after a sale. It talks to storage only through
// IProductRepository, so storage can change without touching it (OCP).
class ProductService
{
public:
    explicit ProductService(IProductRepository& repo) : m_repo(repo) {}

    std::vector<Product> listAll() { return m_repo.loadAll(); }
    std::optional<Product> findByBarcode(const std::string& barcode)
    {
        return m_repo.findByBarcode(barcode);
    }
    std::string productFilePath() const { return m_repo.filePath(); }

    // THE stock rule from the requirements, in exactly one place:
    // a product may be added when the required quantity does not
    // exceed the available quantity (buying the last unit is allowed).
    static bool canSell(const Product& product, int quantity)
    {
        return quantity > 0 && quantity <= product.availableQuantity;
    }

    // After a bill is generated, reduce the stock in Data/product.txt.
    bool decreaseStock(int productId, int quantity);

private:
    IProductRepository& m_repo;
};
