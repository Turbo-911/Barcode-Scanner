#include "services/product_service.h"

namespace
{
// D-21: the service-layer field rules - acronym 1-8 chars, unit in
// {count, weight}, price > 0, tax >= 0, discount >= 0 and <= price,
// quantity >= 0. Returns "" when the product is valid, else "bad-fields".
// v38 (D-06): the acronym is [A-Z0-9]{1,8} - a space (or lowercase, or
// punctuation) breaks the space-separated product line and the product
// silently vanishes on the next load, so it is refused here.
bool isValidAcronym(const std::string& s)
{
    if (s.empty() || s.size() > 8) return false;
    for (const char c : s)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

std::string validateProductFields(const Product& p)
{
    if (p.name.empty()) return "bad-fields";
    if (!isValidAcronym(p.acronym)) return "bad-fields";
    if (p.unit != "count" && p.unit != "weight") return "bad-fields";
    if (!(p.unitPrice > 0.0)) return "bad-fields";
    if (!(p.taxPerUnit >= 0.0)) return "bad-fields";
    if (!(p.discountPerUnit >= 0.0) || p.discountPerUnit > p.unitPrice)
        return "bad-fields";
    if (p.availableQuantity < 0) return "bad-fields";
    return "";
}
}

// D-31: the ONE stock-adjust function. decrease/increase/setStock all
// route through it, so the load-modify-save exists exactly once.
// `absolute` false: add `delta` to the stock (negative delta removes);
// `absolute` true: set the stock to `delta`. Fails when the result
// would go below zero or the product does not exist.
bool ProductService::modifyStock(int productId, int delta, bool absolute)
{
    std::vector<Product> all = m_repo.loadAll();
    for (Product& p : all)
    {
        if (p.id == productId)
        {
            const int next = absolute ? delta : p.availableQuantity + delta;
            if (next < 0) return false;
            p.availableQuantity = next;
            return m_repo.saveAll(all);
        }
    }
    return false;
}

bool ProductService::decreaseStock(int productId, int quantity)
{
    // D-15: a negative decrease would INCREASE the stock.
    if (quantity < 0) return false;
    return modifyStock(productId, -quantity, false);
}

bool ProductService::increaseStock(int productId, int quantity)
{
    // D-15: a negative increase would DECREASE the stock.
    if (quantity < 0) return false;
    return modifyStock(productId, quantity, false);
}

bool ProductService::setStock(int productId, int quantity)
{
    if (quantity < 0) return false;
    return modifyStock(productId, quantity, true);
}

std::string ProductService::addProduct(Product p)
{
    if (const std::string err = validateProductFields(p); !err.empty()) return err;
    if (!m_categories.findById(p.categoryId)) return "bad-category";
    if (p.id <= 0) return "bad-id";

    std::vector<Product> all = m_repo.loadAll();
    for (const Product& existing : all)
    {
        if (existing.id == p.id) return "bad-id";
        if (existing.barcode == p.barcode) return "bad-barcode";
    }
    p.barcode = generateBarcode(p.categoryId, p.id);
    // D-20: makeBarcode returns "" for category "00" or an id that
    // cannot fit the 6-digit scheme (id > 9999).
    if (p.barcode.empty()) return "bad-category";
    for (const Product& existing : all)
        if (existing.barcode == p.barcode) return "bad-barcode";
    // D-21: set barcodeFile exactly as updateProduct does - without it
    // the row is silently dropped on the next load (D-03).
    // D-37: product barcodes live in barcodes/, not tests/.
    p.barcodeFile = "barcodes/barcode_" + p.barcode + ".bmp";

    all.push_back(p);
    return m_repo.saveAll(all) ? "" : "write-failed";
}

std::string ProductService::updateProduct(const Product& p)
{
    if (const std::string err = validateProductFields(p); !err.empty()) return err;
    if (!m_categories.findById(p.categoryId)) return "bad-category";

    std::vector<Product> all = m_repo.loadAll();
    for (Product& existing : all)
    {
        if (existing.id != p.id) continue;
        const bool categoryChanged = existing.categoryId != p.categoryId;
        const std::string oldBarcode = existing.barcode;
        const std::string oldFile = existing.barcodeFile;
        std::string newBarcode = oldBarcode;
        std::string newFile = oldFile;
        if (categoryChanged)
        {
            newBarcode = generateBarcode(p.categoryId, p.id);
            if (newBarcode.empty()) return "bad-category";  // D-20
            // D-20: a regenerated barcode must not collide with another
            // product's barcode (addProduct already checks this).
            for (const Product& other : all)
                if (other.id != p.id && other.barcode == newBarcode)
                    return "bad-barcode";
            newFile = "barcodes/barcode_" + newBarcode + ".bmp";  // D-37
        }
        existing = p;
        // Keep the barcode the product already has when the category is
        // unchanged: it is printed on shelf labels and named by
        // barcodeFile, so silently regenerating it on every edit would
        // break both.
        existing.barcode = newBarcode;
        existing.barcodeFile = newFile;
        return m_repo.saveAll(all) ? "" : "write-failed";
    }
    return "bad-id";
}