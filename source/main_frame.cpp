#include "main_frame.hpp"

#include <fstream>
#include <json.hpp>

#include "about_tab.hpp"
#include "ams_tab.hpp"
#include "apps_tab.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "language_tab.hpp"
#include "list_download_tab.hpp"
#include "tools_tab.hpp"
#include "utils.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;
using json = nlohmann::ordered_json;

namespace {
    constexpr const char AppTitle[] = APP_TITLE;
    constexpr const char AppVersion[] = APP_VERSION;
}  // namespace

MainFrame::MainFrame() : TabFrame()
{
    this->setIcon("romfs:/gui_icon.png");
    this->setTitle(AppTitle);

    s64 freeStorage;

    /* Las dos peticiones de abajo son sincronas y ocurren antes de dibujar
       nada: sin conexion la app se quedaba hasta un minuto en negro esperando
       que vencieran los timeouts, que para el usuario es la app colgada.
       Preguntarle al sistema cuesta nada y ahorra esa espera. */
    const bool conectado = util::hayInternet();

    std::string tag = conectado ? util::getLatestTag(TAGS_INFO) : "";
    this->latestTag = tag;
    this->setFooterText(fmt::format("menus/main/footer_text"_i18n,
                                    (!tag.empty() && tag != AppVersion) ? AppVersion + "menus/main/new_update"_i18n : AppVersion,
                                    R_SUCCEEDED(fs::getFreeStorageSD(freeStorage)) ? (float)freeStorage / 0x40000000 : -1));

    json hideStatus = fs::parseJsonFile(HIDE_TABS_JSON);
    nlohmann::ordered_json nxlinks;
    if (conectado)
        download::getRequest(NXLINKS_URL, nxlinks);

    bool erista = util::isErista();

    if (!util::getBoolValue(hideStatus, "about"))
        this->addTab("menus/main/about"_i18n, new AboutTab(tag));

    /* Separadores entre grupos. Siete pestanas seguidas se leen como una lista
       plana; partidas en estado, descargas y ajustes se ve de un vistazo que
       hace cada grupo. Se anaden solo si el grupo que viene tiene algo, para no
       dejar una raya suelta cuando se ocultan pestanas por hide_tabs.json. */
    const bool hasDownloads = !util::getBoolValue(hideStatus, "atmosphere") ||
                              !util::getBoolValue(hideStatus, "firmwares") ||
                              !util::getBoolValue(hideStatus, "cheats") ||
                              !util::getBoolValue(hideStatus, "apps");
    const bool hasSettings = !util::getBoolValue(hideStatus, "tools") ||
                             !util::getBoolValue(hideStatus, "language");

    const bool hasStatus = !util::getBoolValue(hideStatus, "about");

    if (hasStatus && hasDownloads)
        this->addSeparator();

    if (!util::getBoolValue(hideStatus, "atmosphere"))
        this->addTab("menus/main/update_ams"_i18n, new AmsTab_Regular(nxlinks, erista));

    if (!util::getBoolValue(hideStatus, "firmwares"))
        this->addTab("menus/main/download_firmware"_i18n, new ListDownloadTab(contentType::fw, nxlinks));

    if (!util::getBoolValue(hideStatus, "cheats"))
        this->addTab("menus/main/download_cheats"_i18n, new ListDownloadTab(contentType::cheats));

    if (!util::getBoolValue(hideStatus, "apps"))
        this->addTab("menus/main/apps"_i18n, new AppsTab(nxlinks));

    /* Mira los dos grupos anteriores, no solo el de descargas: ocultandolas
       todas, estado y ajustes quedarian pegados sin separador. Cada separador
       comprueba ademas que su propio grupo tenga algo, asi que no salen dos
       seguidos ni uno al final. */
    if ((hasStatus || hasDownloads) && hasSettings)
        this->addSeparator();

    if (!util::getBoolValue(hideStatus, "tools"))
        this->addTab("menus/main/tools"_i18n, new ToolsTab(tag, util::getValueFromKey(nxlinks, "payloads"), erista, hideStatus));

    if (!util::getBoolValue(hideStatus, "language"))
        this->addTab("menus/tools/language"_i18n, new LanguageTab());

    this->registerAction("", brls::Key::B, [this] { return true; });
}
