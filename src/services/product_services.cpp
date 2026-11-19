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
