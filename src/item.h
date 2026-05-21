#pragma once

#include <string>

struct Item {
    int id = 0;
    std::string name;
    std::string hero;
    std::string rarity;
    std::string type;
    int price = 0;
    std::string imageUrl;
    std::string description;
    std::string collection;
};

