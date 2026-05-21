#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "item.h"

class Database {
public:
    explicit Database(const std::filesystem::path& databasePath);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void initialize();
    std::vector<Item> getAllItems() const;
    std::optional<Item> getItemById(int id) const;
    int createItem(const Item& item);
    bool updateItem(int id, const Item& item);
    bool deleteItem(int id);

private:
    std::filesystem::path databasePath_;

    void execute(const std::string& sql) const;
    std::vector<std::vector<std::string>> query(const std::string& sql) const;
    int countItems() const;
    void seedItems();
};
