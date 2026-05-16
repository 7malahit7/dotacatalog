document.addEventListener("DOMContentLoaded", () => {
    setupCatalogFilters();
    setupDeleteButtons();
    setupItemForm();
});

function setupCatalogFilters() {
    const searchInput = document.querySelector("[data-catalog-search]");
    const rarityFilter = document.querySelector("[data-rarity-filter]");
    const cards = Array.from(document.querySelectorAll("[data-catalog-card]"));
    const emptyState = document.querySelector("[data-empty-state]");

    if (!searchInput || !rarityFilter || cards.length === 0) {
        return;
    }

    const applyFilters = () => {
        const query = searchInput.value.trim().toLowerCase();
        const selectedRarity = rarityFilter.value;
        let visibleCount = 0;

        cards.forEach((card) => {
            const name = card.dataset.name.toLowerCase();
            const hero = card.dataset.hero.toLowerCase();
            const rarity = card.dataset.rarity;
            const matchesText = name.includes(query) || hero.includes(query);
            const matchesRarity = selectedRarity === "" || rarity === selectedRarity;
            const shouldShow = matchesText && matchesRarity;

            card.classList.toggle("is-hidden", !shouldShow);
            visibleCount += shouldShow ? 1 : 0;
        });

        if (emptyState) {
            emptyState.hidden = visibleCount > 0;
        }
    };

    searchInput.addEventListener("input", applyFilters);
    rarityFilter.addEventListener("change", applyFilters);
}

function setupDeleteButtons() {
    document.querySelectorAll("[data-delete]").forEach((button) => {
        button.addEventListener("click", () => {
            const itemName = button.dataset.itemName || "выбранный предмет";
            const confirmed = confirm(`Удалить "${itemName}" из каталога?`);

            if (confirmed) {
                button.closest("tr")?.remove();
            }
        });
    });
}

function setupItemForm() {
    const form = document.querySelector("[data-item-form]");
    const message = document.querySelector("[data-form-message]");

    if (!form || !message) {
        return;
    }

    form.addEventListener("submit", (event) => {
        const requiredFields = Array.from(form.querySelectorAll("[required]"));
        const emptyField = requiredFields.find((field) => field.value.trim() === "");

        if (emptyField) {
            event.preventDefault();
            message.textContent = "Заполните обязательные поля перед сохранением.";
            message.className = "form-message is-error";
            emptyField.focus();
            return;
        }

        if (form.hasAttribute("data-prototype-form")) {
            event.preventDefault();
            message.textContent = "Форма заполнена корректно. Backend подключим следующим шагом.";
            message.className = "form-message is-success";
        }
    });
}
