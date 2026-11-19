#include "persistence/i_product_repo.h"
#include "persistence/product_serialiser.h"
#include "persistence/record_file.h"

// ProductFileRepository - member definitions (declared in i_product_repo.h).

ProductFileRepository::ProductFileRepository(std::string fileName)
    : m_path(RecordFile::locate({fileName, "../" + fileName, "Barcode-Scanner/" + fileName}))
{
}

std::vector<Product> ProductFileRepository::loadAll()
{
    std::vector<Product> products;
    for (const std::string& line : RecordFile::readAllLines(m_path))
        if (auto p = ProductSerialiser::parse(line)) products.push_back(*p);
    return products;
}

std::optional<Product> ProductFileRepository::findByBarcode(const std::string& barcode)
{
    for (const Product& p : loadAll())
        if (p.barcode == barcode) return p;
    return std::nullopt;
}

std::optional<Product> ProductFileRepository::findById(int id)
{
    for (const Product& p : loadAll())
        if (p.id == id) return p;
    return std::nullopt;
}

bool ProductFileRepository::saveAll(const std::vector<Product>& products)
{
    std::vector<std::string> lines;
    for (const Product& p : products) lines.push_back(ProductSerialiser::format(p));
    return RecordFile::writeAllLines(m_path, lines);
}
