#pragma once
#include "domain/product.h"
#include "persistence/i_product_repo.h"
#include "services/category_service.h"
#include <optional>
#include <string>
#include <vector>

// ProductService - everything the app DOES with products (SRP):
// lookups for the description feature, the stock rule, stock updates
// after a sale, and the manager operations (add / edit / update stock /
// generate a category-prefixed barcode). It talks to storage only
// through IProductRepository, so storage can change without touching it.
class ProductService
{
public:
    ProductService(IProductRepository& repo, CategoryService& categories)
        : m_repo(repo), m_categories(categories) {}

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

    // Reduce stock after a sale (called by OrderService).
    bool decreaseStock(int productId, int quantity);
    // Restore stock when a paid order is edited / cancelled.
    bool increaseStock(int productId, int quantity);
    // Direct inventory update (manager screen, requirement 4).
    bool setStock(int productId, int quantity);

    // Build a 6-digit category-prefixed barcode for a new product,
    // based on its id and the id of the category it belongs to.
    static std::string generateBarcode(const std::string& categoryId, int productId)
    {
        return CategoryService::makeBarcode(categoryId, productId);
    }

    // Manager: add a new product. The barcode must be empty in the call
    // and is generated from the product id + category id. Returns "" on
    // success, else:
    //   "bad-category"  - category id is unknown, "00", or cannot form a barcode
    //   "bad-id"        - id <= 0 or already used
    //   "bad-barcode"   - barcode already used
    //   "bad-fields"    - D-21 field rules: name/acronym (1-8 chars), unit
    //                     count|weight, price > 0, tax >= 0, 0 <= discount
    //                     <= price, quantity >= 0
    std::string addProduct(Product p);

    // Manager: replace a product (matched by id). Its category id must
    // already exist. The barcode is regenerated so its prefix matches
    // the category id. Returns "" on success or one of the codes above.
    std::string updateProduct(const Product& p);

private:
    // D-31: the ONE stock-adjust function (see product_services.cpp).
    bool modifyStock(int productId, int delta, bool absolute);

    IProductRepository& m_repo;
    CategoryService& m_categories;
};