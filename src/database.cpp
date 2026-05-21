#include "database.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace {

std::string shellQuote(const std::string& value) {
    std::string result = "'";
    for (char ch : value) {
        if (ch == '\'') {
            result += "'\\''";
        } else {
            result += ch;
        }
    }
    result += "'";
    return result;
}

std::string sqlQuote(const std::string& value) {
    std::string result = "'";
    for (char ch : value) {
        if (ch == '\'') {
            result += "''";
        } else {
            result += ch;
        }
    }
    result += "'";
    return result;
}

std::vector<std::string> splitRow(const std::string& row) {
    std::vector<std::string> columns;
    std::size_t start = 0;

    while (start <= row.size()) {
        const std::size_t end = row.find('|', start);
        columns.push_back(row.substr(start, end == std::string::npos ? std::string::npos : end - start));

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return columns;
}

int toInt(const std::string& value) {
    try {
        return std::stoi(value);
    } catch (...) {
        return 0;
    }
}

Item rowToItem(const std::vector<std::string>& row) {
    Item item;
    if (row.size() < 9) {
        return item;
    }

    item.id = toInt(row[0]);
    item.name = row[1];
    item.hero = row[2];
    item.rarity = row[3];
    item.type = row[4];
    item.price = toInt(row[5]);
    item.imageUrl = row[6];
    item.description = row[7];
    item.collection = row[8];
    return item;
}

std::string writeTempSqlFile(const std::filesystem::path& databasePath, const std::string& sql) {
    const std::filesystem::path tempPath = databasePath.parent_path() / (".query_" + std::to_string(getpid()) + ".sql");
    std::ofstream file(tempPath);
    if (!file) {
        throw std::runtime_error("Cannot create temporary SQL file");
    }

    file << sql << '\n';
    return tempPath.string();
}

}  // namespace

Database::Database(const std::filesystem::path& databasePath) : databasePath_(databasePath) {
    std::filesystem::create_directories(databasePath_.parent_path());
}

Database::~Database() = default;

void Database::initialize() {
    execute(
        "CREATE TABLE IF NOT EXISTS items ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL,"
        "hero TEXT NOT NULL,"
        "rarity TEXT NOT NULL,"
        "type TEXT NOT NULL,"
        "price INTEGER NOT NULL DEFAULT 0,"
        "image_url TEXT,"
        "description TEXT,"
        "collection TEXT"
        ");"
    );

    if (countItems() == 0) {
        seedItems();
    }
}

std::vector<Item> Database::getAllItems() const {
    const auto rows = query(
        "SELECT id, name, hero, rarity, type, price, IFNULL(image_url, ''), IFNULL(description, ''), IFNULL(collection, '') "
        "FROM items ORDER BY id DESC;"
    );

    std::vector<Item> items;
    for (const auto& row : rows) {
        items.push_back(rowToItem(row));
    }
    return items;
}

std::optional<Item> Database::getItemById(int id) const {
    const auto rows = query(
        "SELECT id, name, hero, rarity, type, price, IFNULL(image_url, ''), IFNULL(description, ''), IFNULL(collection, '') "
        "FROM items WHERE id = " + std::to_string(id) + " LIMIT 1;"
    );

    if (rows.empty()) {
        return std::nullopt;
    }

    return rowToItem(rows.front());
}

int Database::createItem(const Item& item) {
    execute(
        "INSERT INTO items (name, hero, rarity, type, price, image_url, description, collection) VALUES (" +
        sqlQuote(item.name) + ", " +
        sqlQuote(item.hero) + ", " +
        sqlQuote(item.rarity) + ", " +
        sqlQuote(item.type) + ", " +
        std::to_string(item.price) + ", " +
        sqlQuote(item.imageUrl) + ", " +
        sqlQuote(item.description) + ", " +
        sqlQuote(item.collection) + ");"
    );
    return 0;
}

bool Database::updateItem(int id, const Item& item) {
    execute(
        "UPDATE items SET "
        "name = " + sqlQuote(item.name) + ", "
        "hero = " + sqlQuote(item.hero) + ", "
        "rarity = " + sqlQuote(item.rarity) + ", "
        "type = " + sqlQuote(item.type) + ", "
        "price = " + std::to_string(item.price) + ", "
        "image_url = " + sqlQuote(item.imageUrl) + ", "
        "description = " + sqlQuote(item.description) + ", "
        "collection = " + sqlQuote(item.collection) + " "
        "WHERE id = " + std::to_string(id) + ";"
    );
    return true;
}

bool Database::deleteItem(int id) {
    execute("DELETE FROM items WHERE id = " + std::to_string(id) + ";");
    return true;
}

void Database::execute(const std::string& sql) const {
    const std::string tempSqlPath = writeTempSqlFile(databasePath_, sql);
    const std::string command = "sqlite3 " + shellQuote(databasePath_.string()) + " < " + shellQuote(tempSqlPath);
    const int result = std::system(command.c_str());
    std::filesystem::remove(tempSqlPath);

    if (result != 0) {
        throw std::runtime_error("SQLite command failed");
    }
}

std::vector<std::vector<std::string>> Database::query(const std::string& sql) const {
    const std::string tempSqlPath = writeTempSqlFile(databasePath_, sql);
    const std::string command =
        "sqlite3 -batch -noheader -separator '|' " +
        shellQuote(databasePath_.string()) +
        " < " +
        shellQuote(tempSqlPath);

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        std::filesystem::remove(tempSqlPath);
        throw std::runtime_error("Cannot run sqlite3 command");
    }

    std::array<char, 4096> buffer = {};
    std::string output;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        output += buffer.data();
    }

    const int result = pclose(pipe);
    std::filesystem::remove(tempSqlPath);
    if (result != 0) {
        throw std::runtime_error("SQLite query failed");
    }

    std::vector<std::vector<std::string>> rows;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            rows.push_back(splitRow(line));
        }
    }

    return rows;
}

int Database::countItems() const {
    const auto rows = query("SELECT COUNT(*) FROM items;");
    if (rows.empty() || rows.front().empty()) {
        return 0;
    }
    return toInt(rows.front().front());
}

void Database::seedItems() {
    createItem({
        0,
        "Manifold Paradox",
        "Phantom Assassin",
        "Immortal",
        "Weapon",
        2499,
        "",
        "Клинки Phantom Assassin с изменяемой формой и особым эффектом убийства.",
        "Immortal Treasure"
    });

    createItem({
        0,
        "Demon Eater",
        "Shadow Fiend",
        "Arcana",
        "Head",
        1790,
        "",
        "Аркана Shadow Fiend с изменением внешнего вида, анимаций и эффектов способностей.",
        "Arcana Collection"
    });

    createItem({
        0,
        "Dark Artistry Cape",
        "Invoker",
        "Mythical",
        "Back",
        640,
        "",
        "Плащ из набора Dark Artistry для Invoker.",
        "Collector's Cache"
    });

    createItem({
        0,
        "Bladeform Legacy",
        "Juggernaut",
        "Arcana",
        "Weapon",
        2190,
        "",
        "Маска и клинок Juggernaut с альтернативным стилем и улучшенными эффектами.",
        "Arcana Collection"
    });
}

