#include "language_tab.hpp"

#include <filesystem>
#include <fstream>
#include <json.hpp>

#include "constants.hpp"
#include "utils.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;
using json = nlohmann::ordered_json;

namespace {
    void confirmAndQuit(std::function<void()> onConfirm)
    {
        brls::Dialog* dialog = new brls::Dialog("menus/tools/language_restart"_i18n);
        dialog->addButton("OK", [onConfirm](brls::View* view) {
            onConfirm();
            brls::Application::quit();
        });
        dialog->open();
    }
}

LanguageTab::LanguageTab() : brls::List()
{
    std::vector<std::pair<std::string, std::string>> languages{
        std::make_pair("American English ({})", "en-US"),
        std::make_pair("Español ({})", "es"),
        std::make_pair("Português ({})", "pt-BR")};

    brls::ListItem* listItem;
    listItem = new brls::ListItem(fmt::format("System Default ({})", i18n::getCurrentLocale()));
    listItem->registerAction("menus/tools/language"_i18n, brls::Key::A, [] {
        confirmAndQuit([] { std::filesystem::remove(LANGUAGE_JSON); });
        return true;
    });
    listItem->setHeight(LISTITEM_HEIGHT);
    this->addView(listItem);

    for (auto& language : languages) {
        if (std::filesystem::exists(fmt::format(LOCALISATION_FILE, language.second))) {
            listItem = new brls::ListItem(fmt::format(language.first, language.second));
            listItem->registerAction("menus/tools/language"_i18n, brls::Key::A, [language] {
                confirmAndQuit([language] {
                    json updatedLanguage = json::object();
                    updatedLanguage["language"] = language.second;
                    std::ofstream out(LANGUAGE_JSON);
                    out << updatedLanguage.dump();
                });
                return true;
            });
            listItem->setHeight(LISTITEM_HEIGHT);
            this->addView(listItem);
        }
    }
}
