#include "apps_tab.hpp"

#include "list_download_tab.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {

    // Clave en nx-links.json y su entrada en el menu. El orden es el que se ve.
    struct Section
    {
        const char* jsonKey;
        const char* label;
    };

    constexpr Section SECTIONS[] = {
        {"apps_emuladores", "menus/apps/sec_emuladores"},
        {"apps_mods", "menus/apps/sec_mods"},
        {"apps_sysmodules", "menus/apps/sec_sysmodules"},
        {"apps_instaladores", "menus/apps/sec_instaladores"},
        {"apps_sistema", "menus/apps/sec_sistema"},
    };

}  // namespace

AppsTab::AppsTab(const nlohmann::ordered_json& nxlinks) : brls::List(), nxlinks(nxlinks)
{
    /* Primero el buscador, antes que las secciones: cuando se busca una app por
       nombre no se sabe en que seccion cayo, y tener que adivinarla para poder
       buscar es justo el paso que sobra. Busca en todo el catalogo. */
    brls::ListItem* search = new brls::ListItem("menus/search/entry"_i18n);
    search->setHeight(LISTITEM_HEIGHT);
    {
        const nlohmann::ordered_json links = this->nxlinks;
        search->getClickEvent()->subscribe([links](brls::View* view) {
            ListDownloadTab::openSearch(contentType::apps, links);
        });
    }
    this->addView(search);

    for (const auto& section : SECTIONS) {
        brls::ListItem* item = new brls::ListItem(i18n::getStr(section.label));
        item->setHeight(LISTITEM_HEIGHT);

        const nlohmann::ordered_json links = this->nxlinks;
        const std::string key = section.jsonKey;
        const std::string title = i18n::getStr(section.label);

        item->getClickEvent()->subscribe([links, key, title](brls::View* view) {
            /* PopupFrame necesita un AppletFrame, asi que la lista va dentro de uno. */
            brls::AppletFrame* frame = new brls::AppletFrame(true, true);
            frame->setContentView(new ListDownloadTab(contentType::apps, links, key));
            brls::PopupFrame::open(title, frame, "", "");
        });

        this->addView(item);
    }

    // Todo el catalogo junto, por si algo no encaja en ninguna seccion o el
    // pack todavia no trae las claves nuevas.
    brls::ListItem* all = new brls::ListItem("menus/apps/sec_todas"_i18n);
    all->setHeight(LISTITEM_HEIGHT);
    const nlohmann::ordered_json links = this->nxlinks;
    all->getClickEvent()->subscribe([links](brls::View* view) {
        brls::AppletFrame* frame = new brls::AppletFrame(true, true);
        frame->setContentView(new ListDownloadTab(contentType::apps, links));
        brls::PopupFrame::open("menus/apps/sec_todas"_i18n, frame, "", "");
    });
    this->addView(all);
}
