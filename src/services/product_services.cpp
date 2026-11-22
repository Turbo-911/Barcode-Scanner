#include "services/product_service.h"

bool ProductService::decreaseStock(int productId, int quantity)
{
    std::vector<Product> all = m_repo.loadAll();
    for (Product& p : all)
    {
        if (p.id == productId)
        {
            if (quantity > p.availableQuantity) return false;
            p.availableQuantity -= quantity;
            return m_repo.saveAll(all);
        }
    }
    return false;
}

bool ProductService::increaseStock(int productId, int quantity)
{
    std::vector<Product> all = m_repo.loadAll();
    for (Product& p : all)
    {
        if (p.id == productId)
        {
            p.availableQuantity += quantity;
            return m_repo.saveAll(all);
        }
    }
    return false;
}

bool ProductService::setStock(int productId, int quantity)
{
    if (quantity < 0) return false;
    std::vector<Product> all = m_repo.loadAll();
    for (Product& p : all)
    {
        if (p.id == productId)
        {
            p.availableQuantity = quantity;
            return m_repo.saveAll(all);
        }
    }
    return false;
}

std::string ProductService::addProduct(Product p)
{
    if (p.name.empty() || p.acronym.empty()) return "bad-fields";
    if (!m_categories.findById(p.categoryId)) return "bad-category";
    if (p.id <= 0) return "bad-id";

    std::vector<Product> all = m_repo.loadAll();
    for (const Product& existing : all)
    {
        if (existing.id == p.id) return "bad-id";
        if (existing.barcode == p.barcode) return "bad-barcode";
    }
    p.barcode = generateBarcode(p.categoryId, p.id);
    for (const Product& existing : all)
        if (existing.barcode == p.barcode) return "bad-barcode";

    all.push_back(p);
    return m_repo.saveAll(all) ? "" : "write-failed";
}

std::string ProductService::updateProduct(const Product& p)
{
    if (p.name.empty() || p.acronym.empty()) return "bad-fields";
    if (!m_categories.findById(p.categoryId)) return "bad-category";

    std::vector<Product> all = m_repo.loadAll();
    for (Product& existing : all)
    {
        if (existing.id != p.id) continue;
        const bool categoryChanged = existing.categoryId != p.categoryId;
        const std::string oldBarcode = existing.barcode;
        const std::string oldFile = existing.barcodeFile;
        existing = p;
        if (!categoryChanged)
        {
            // Keep the barcode the product already has: it is printed
            // on shelf labels and named by barcodeFile, so silently
            // regenerating it on every edit would break both.
            existing.barcode = oldBarcode;
            existing.barcodeFile = oldFile;
        }
        else
        {
            existing.barcode = generateBarcode(p.categoryId, p.id);
            existing.barcodeFile = "tests/barcode_" + existing.barcode + ".bmp";
        }
        return m_repo.saveAll(all) ? "" : "write-failed";
    }
    return "bad-id";
}