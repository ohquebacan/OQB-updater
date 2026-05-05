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
        std::make_pair("日本語 ({})", "ja"),
        std::make_pair("Français ({})", "fr"),
        std::make_pair("Deutsch ({})", "de"),
        std::make_pair("Italiano ({})", "it"),
        std::make_pair("Español ({})", "es"),
        std::make_pair("Português ({})", "pt-BR"),
        std::make_pair("Русский ({})", "ru"),
        std::make_pair("Română ({})", "ro"),
        std::make_pair("한국어 ({})", "ko"),
        std::make_pair("Polski ({})", "pl"),
        std::make_pair("简体中文 ({})", "zh-CN"),
        std::make_pair("繁體中文 ({})", "zh-TW"),
        std::make_pair("Español (Latinoamérica) ({})", "es-419")};

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
