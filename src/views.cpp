#include "views.h"

#include <sstream>

namespace views {
namespace {

std::string escapeHtml(const std::string& value) {
    std::string result;
    result.reserve(value.size());

    for (char ch : value) {
        switch (ch) {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            case '"':
                result += "&quot;";
                break;
            case '\'':
                result += "&#39;";
                break;
            default:
                result += ch;
                break;
        }
    }

    return result;
}

std::string rarityClass(const std::string& rarity) {
    if (rarity == "Arcana") {
        return "rarity-arcana";
    }
    if (rarity == "Immortal") {
        return "rarity-immortal";
    }
    if (rarity == "Mythical") {
        return "rarity-mythical";
    }
    return "";
}

std::string renderLayout(const std::string& title, const std::string& content) {
    std::ostringstream html;
    html
        << "<!DOCTYPE html>\n"
        << "<html lang=\"ru\">\n"
        << "<head>\n"
        << "    <meta charset=\"UTF-8\">\n"
        << "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        << "    <title>" << escapeHtml(title) << " - Dota Relics Catalog</title>\n"
        << "    <link rel=\"stylesheet\" href=\"/static/css/style.css\">\n"
        << "</head>\n"
        << "<body>\n"
        << "    <header class=\"site-header\">\n"
        << "        <a class=\"logo\" href=\"/\">Dota Relics</a>\n"
        << "        <nav class=\"main-nav\">\n"
        << "            <a href=\"/\">Каталог</a>\n"
        << "            <a href=\"/admin\">Админка</a>\n"
        << "        </nav>\n"
        << "    </header>\n"
        << content
        << "    <script src=\"/static/js/app.js\"></script>\n"
        << "</body>\n"
        << "</html>\n";
    return html.str();
}

std::string renderImageBlock(const Item& item, const std::string& cssClass) {
    if (!item.imageUrl.empty()) {
        return "<div class=\"" + cssClass + "\"><img src=\"" + escapeHtml(item.imageUrl) + "\" alt=\"" + escapeHtml(item.name) + "\"></div>";
    }

    return "<div class=\"" + cssClass + "\">IMG</div>";
}

std::string selected(const std::string& current, const std::string& value) {
    return current == value ? " selected" : "";
}

}  // namespace

std::string renderCatalogPage(const std::vector<Item>& items) {
    std::ostringstream content;
    content
        << "    <main class=\"page\">\n"
        << "        <aside class=\"sidebar\">\n"
        << "            <h2>Коллекции</h2>\n"
        << "            <a href=\"#\">Arcana</a>\n"
        << "            <a href=\"#\">Immortal</a>\n"
        << "            <a href=\"#\">Mythical</a>\n"
        << "            <a href=\"#\">Collector's Cache</a>\n"
        << "        </aside>\n"
        << "        <section class=\"content\">\n"
        << "            <div class=\"page-title\">\n"
        << "                <h1>Каталог предметов Dota 2</h1>\n"
        << "                <p>Публичная страница со списком предметов из базы данных.</p>\n"
        << "            </div>\n"
        << "            <div class=\"toolbar\">\n"
        << "                <input type=\"search\" placeholder=\"Поиск по названию или герою\" data-catalog-search>\n"
        << "                <select data-rarity-filter>\n"
        << "                    <option value=\"\">Все редкости</option>\n"
        << "                    <option value=\"Mythical\">Mythical</option>\n"
        << "                    <option value=\"Immortal\">Immortal</option>\n"
        << "                    <option value=\"Arcana\">Arcana</option>\n"
        << "                </select>\n"
        << "            </div>\n"
        << "            <div class=\"catalog-grid\">\n";

    for (const Item& item : items) {
        content
            << "                <article class=\"item-card\" data-catalog-card data-name=\"" << escapeHtml(item.name)
            << "\" data-hero=\"" << escapeHtml(item.hero)
            << "\" data-rarity=\"" << escapeHtml(item.rarity) << "\">\n"
            << "                    " << renderImageBlock(item, "item-image") << "\n"
            << "                    <div class=\"item-meta\">\n"
            << "                        <span class=\"rarity " << rarityClass(item.rarity) << "\">" << escapeHtml(item.rarity) << "</span>\n"
            << "                        <span>" << escapeHtml(item.hero) << "</span>\n"
            << "                    </div>\n"
            << "                    <h2>" << escapeHtml(item.name) << "</h2>\n"
            << "                    <p>" << escapeHtml(item.description) << "</p>\n"
            << "                    <div class=\"card-footer\">\n"
            << "                        <strong>" << item.price << " ₽</strong>\n"
            << "                        <a href=\"/items/" << item.id << "\">Подробнее</a>\n"
            << "                    </div>\n"
            << "                </article>\n";
    }

    content
        << "            </div>\n"
        << "            <p class=\"empty-state\" data-empty-state hidden>Ничего не найдено. Попробуйте изменить поиск или фильтр.</p>\n"
        << "        </section>\n"
        << "    </main>\n";

    return renderLayout("Каталог", content.str());
}

std::string renderItemPage(const Item& item) {
    std::ostringstream content;
    content
        << "    <main class=\"page single-page\">\n"
        << "        <section class=\"detail-card\">\n"
        << "            " << renderImageBlock(item, "detail-image") << "\n"
        << "            <div class=\"detail-content\">\n"
        << "                <a class=\"back-link\" href=\"/\">Назад в каталог</a>\n"
        << "                <span class=\"rarity " << rarityClass(item.rarity) << "\">" << escapeHtml(item.rarity) << "</span>\n"
        << "                <h1>" << escapeHtml(item.name) << "</h1>\n"
        << "                <p>" << escapeHtml(item.description) << "</p>\n"
        << "                <dl class=\"item-info\">\n"
        << "                    <dt>Герой</dt><dd>" << escapeHtml(item.hero) << "</dd>\n"
        << "                    <dt>Тип</dt><dd>" << escapeHtml(item.type) << "</dd>\n"
        << "                    <dt>Коллекция</dt><dd>" << escapeHtml(item.collection) << "</dd>\n"
        << "                    <dt>Цена</dt><dd>" << item.price << " ₽</dd>\n"
        << "                </dl>\n"
        << "            </div>\n"
        << "        </section>\n"
        << "    </main>\n";

    return renderLayout(item.name, content.str());
}

std::string renderAdminPage(const std::vector<Item>& items) {
    std::ostringstream content;
    content
        << "    <main class=\"admin-page\">\n"
        << "        <div class=\"admin-header\">\n"
        << "            <div>\n"
        << "                <h1>Управление предметами</h1>\n"
        << "                <p>Открытая админка для добавления, редактирования и удаления записей.</p>\n"
        << "            </div>\n"
        << "            <a class=\"button\" href=\"/admin/new\">Добавить предмет</a>\n"
        << "        </div>\n"
        << "        <table class=\"admin-table\">\n"
        << "            <thead><tr><th>ID</th><th>Название</th><th>Герой</th><th>Редкость</th><th>Цена</th><th>Действия</th></tr></thead>\n"
        << "            <tbody>\n";

    for (const Item& item : items) {
        content
            << "                <tr>\n"
            << "                    <td>" << item.id << "</td>\n"
            << "                    <td>" << escapeHtml(item.name) << "</td>\n"
            << "                    <td>" << escapeHtml(item.hero) << "</td>\n"
            << "                    <td>" << escapeHtml(item.rarity) << "</td>\n"
            << "                    <td>" << item.price << " ₽</td>\n"
            << "                    <td>\n"
            << "                        <a href=\"/admin/edit/" << item.id << "\">Редактировать</a>\n"
            << "                        <form class=\"inline-form\" method=\"post\" action=\"/admin/delete/" << item.id << "\">\n"
            << "                            <button type=\"submit\" data-delete data-item-name=\"" << escapeHtml(item.name) << "\">Удалить</button>\n"
            << "                        </form>\n"
            << "                    </td>\n"
            << "                </tr>\n";
    }

    content
        << "            </tbody>\n"
        << "        </table>\n"
        << "    </main>\n";

    return renderLayout("Админка", content.str());
}

std::string renderItemFormPage(const std::optional<Item>& item) {
    const bool editing = item.has_value();
    const Item value = item.value_or(Item{});
    const std::string action = editing ? "/admin/update/" + std::to_string(value.id) : "/admin/create";

    std::ostringstream content;
    content
        << "    <main class=\"admin-page\">\n"
        << "        <div class=\"admin-header\">\n"
        << "            <div>\n"
        << "                <h1>" << (editing ? "Редактирование предмета" : "Добавление предмета") << "</h1>\n"
        << "                <p>Заполните поля и сохраните запись в SQLite.</p>\n"
        << "            </div>\n"
        << "            <a class=\"button button-secondary\" href=\"/admin\">Назад</a>\n"
        << "        </div>\n"
        << "        <form class=\"item-form\" method=\"post\" action=\"" << action << "\" data-item-form novalidate>\n"
        << "            <label>Название<input type=\"text\" name=\"name\" value=\"" << escapeHtml(value.name) << "\" placeholder=\"Manifold Paradox\" required></label>\n"
        << "            <label>Герой<input type=\"text\" name=\"hero\" value=\"" << escapeHtml(value.hero) << "\" placeholder=\"Phantom Assassin\" required></label>\n"
        << "            <label>Редкость<select name=\"rarity\" required>\n"
        << "                <option value=\"\">Выберите редкость</option>\n"
        << "                <option" << selected(value.rarity, "Common") << ">Common</option>\n"
        << "                <option" << selected(value.rarity, "Rare") << ">Rare</option>\n"
        << "                <option" << selected(value.rarity, "Mythical") << ">Mythical</option>\n"
        << "                <option" << selected(value.rarity, "Immortal") << ">Immortal</option>\n"
        << "                <option" << selected(value.rarity, "Arcana") << ">Arcana</option>\n"
        << "            </select></label>\n"
        << "            <label>Тип<input type=\"text\" name=\"type\" value=\"" << escapeHtml(value.type) << "\" placeholder=\"Weapon\" required></label>\n"
        << "            <label>Цена<input type=\"number\" name=\"price\" min=\"0\" value=\"" << value.price << "\" placeholder=\"2499\" required></label>\n"
        << "            <label>Коллекция<input type=\"text\" name=\"collection\" value=\"" << escapeHtml(value.collection) << "\" placeholder=\"Arcana Collection\"></label>\n"
        << "            <label>Ссылка на изображение<input type=\"text\" name=\"image_url\" value=\"" << escapeHtml(value.imageUrl) << "\" placeholder=\"/static/img/item.png\"></label>\n"
        << "            <label>Описание<textarea name=\"description\" rows=\"5\" placeholder=\"Краткое описание предмета\">" << escapeHtml(value.description) << "</textarea></label>\n"
        << "            <button class=\"button\" type=\"submit\">Сохранить</button>\n"
        << "            <p class=\"form-message\" data-form-message></p>\n"
        << "        </form>\n"
        << "    </main>\n";

    return renderLayout(editing ? "Редактирование" : "Добавление", content.str());
}

std::string renderNotFoundPage(const std::string& message) {
    std::ostringstream content;
    content
        << "    <main class=\"admin-page\">\n"
        << "        <h1>404 Not Found</h1>\n"
        << "        <p>" << escapeHtml(message) << "</p>\n"
        << "        <a class=\"button\" href=\"/\">Вернуться в каталог</a>\n"
        << "    </main>\n";
    return renderLayout("404", content.str());
}

std::string renderServerErrorPage(const std::string& message) {
    std::ostringstream content;
    content
        << "    <main class=\"admin-page\">\n"
        << "        <h1>500 Server Error</h1>\n"
        << "        <p>" << escapeHtml(message) << "</p>\n"
        << "    </main>\n";
    return renderLayout("Ошибка", content.str());
}

}  // namespace views

