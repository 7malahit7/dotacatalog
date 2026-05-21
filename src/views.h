#pragma once

#include <optional>
#include <string>
#include <vector>

#include "item.h"

namespace views {

std::string renderCatalogPage(const std::vector<Item>& items);
std::string renderItemPage(const Item& item);
std::string renderAdminPage(const std::vector<Item>& items);
std::string renderItemFormPage(const std::optional<Item>& item);
std::string renderNotFoundPage(const std::string& message);
std::string renderServerErrorPage(const std::string& message);

}  // namespace views

