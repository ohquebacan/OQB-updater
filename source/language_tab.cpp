#include "language_tab.hpp"

#include <filesystem>
#include <fstream>
#include <json.hpp>

#include "constants.hpp"
#include "utils.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;
using json = nlohmann::ordered_json;

LanguageTab::LanguageTab() : brls::List()
{
    std::vector<std::pair<std::string, std::string>> languages{
        std::make_pair("American English ({})", "en-US"),
        std::make_pair("Español ({})", "es"),
        std::make_pair("Português ({})", "pt-BR")};

    brls::ListItem* listItem;
    listItem = new brls::ListItem(fmt::format("System Default ({})", i18n::getCurrentLocale()));
    listItem->registerAction("menus/tools/language"_i18n, brls::Key::A, [] {
        std::filesystem::remove(LANGUAGE_JSON);
        brls::Application::quit();
        return true;
    });
    listItem->setHeight(LISTITEM_HEIGHT);
    this->addView(listItem);

    for (auto& language : languages) {
        if (std::filesystem::exists(fmt::format(LOCALISATION_FILE, language.second))) {
            listItem = new brls::ListItem(fmt::format(language.first, language.second));
            listItem->registerAction("menus/tools/language"_i18n, brls::Key::A, [language] {
                json updatedLanguage = json::object();
                updatedLanguage["language"] = language.second;
                std::ofstream out(LANGUAGE_JSON);
                out << updatedLanguage.dump();
                brls::Application::quit();
                return true;
            });
            listItem->setHeight(LISTITEM_HEIGHT);
            this->addView(listItem);
        }
    }
}
