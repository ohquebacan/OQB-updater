#include "main_frame.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <json.hpp>
#include <thread>

#include "about_tab.hpp"
#include "arranque.hpp"
#include "ams_tab.hpp"
#include "app_updates.hpp"
#include "apps_tab.hpp"
#include "bacan_shop_tab.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "language_tab.hpp"
#include "list_download_tab.hpp"
#include "nextendo_tab.hpp"
#include "protection.hpp"
#include "tools_tab.hpp"
#include "utils.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;
using json = nlohmann::ordered_json;

namespace {
    constexpr const char AppTitle[] = APP_TITLE;
    constexpr const char AppVersion[] = APP_VERSION;

    /* Lo que la pantalla de inicio pide a internet. La pantalla se vuelve a
       armar cada vez que se termina una descarga ("Volver al inicio"); el
       catalogo no cambia en una sesion, asi que se consulta una vez y se
       reutiliza (antes el boton tardaba varios segundos en responder). */
    bool consultado = false;
    std::string tagGuardado;
    nlohmann::ordered_json nxlinksGuardado;

    /* Las protecciones de la primera ventana, calculadas en segundo plano
       mientras se ve la pantalla de carga. Solo valen para esa ventana: al
       volver al inicio se vuelven a mirar, por si se cambio algo en Ajustes. */
    bool hayPrecargadas = false;
    std::vector<protection::Check> precargadas;

    /* Las consultas van en paralelo: cada una es un viaje de ida y vuelta
       a un servidor distinto y en paralelo cuesta lo que la mas lenta. curl es
       seguro entre hilos: el global se inicializa una sola vez en main(). */
    void consultarRed()
    {
        if (consultado || !util::hayInternet())
            return;
        std::string tagObtenido;
        nlohmann::ordered_json nxObtenido, catalogoObtenido, partidasObtenidas;
        std::thread h1([&] {
            tagObtenido = util::getLatestTag(TAGS_INFO);
            arranque::marca("red: tag listo");
        });
        std::thread h2([&] {
            download::getRequest(NXLINKS_URL, nxObtenido);
            arranque::marca("red: nx-links listo");
        });
        std::thread h3([&] {
            catalogoObtenido = appUpdates::fetchPublished();
            arranque::marca("red: catalogo listo");
        });
        std::thread h4([] {
            util::getCheatsVersion();  // queda guardada para la pestana Trucos
            arranque::marca("red: version de trucos lista");
        });
        std::thread h5([&] {
            // Pocos KB. Si todavia no existe (404) queda vacio y no se muestra nada.
            if (download::getRequest(PARTIDAS_URL, partidasObtenidas) != 200)
                partidasObtenidas = nlohmann::ordered_json::object();
            arranque::marca("red: partidas listas");
        });
        h1.join();
        h2.join();
        h3.join();
        h4.join();
        h5.join();
        // Los iconos de las apps, en segundo plano: solo baja algo si falta alguno.
        if (catalogoObtenido.is_object() && !catalogoObtenido.empty())
            std::thread([c = catalogoObtenido] { appUpdates::asegurarIconos(c); }).detach();
        tagGuardado = tagObtenido;
        nxlinksGuardado = nxObtenido;
        if (nxlinksGuardado.is_object()) {
            nxlinksGuardado[appUpdates::NXLINKS_KEY] = catalogoObtenido;
            nxlinksGuardado["_partidas"] = partidasObtenidas;
        }
        consultado = nxlinksGuardado.is_object() && !nxlinksGuardado.empty();
    }
}  // namespace

void MainFrame::precargar()
{
    arranque::marca("precarga: inicio");
    /* La comprobacion de protecciones hace una consulta DNS: va en su propio
       hilo, a la par de la red. */
    std::thread prot([] {
        precargadas = protection::runOnConsole();
        hayPrecargadas = true;
        arranque::marca("precarga: protecciones listas");
    });
    consultarRed();
    prot.join();
    arranque::marca("precarga: fin");
}

MainFrame::MainFrame() : TabFrame()
{
    this->setIcon("romfs:/gui_icon.png");
    this->setTitle(AppTitle);

    /* Las pestañas preguntan por cada app del catálogo si está en /switch:
       con esto /switch se lee una sola vez para toda la ventana. */
    util::LecturaSwitch lecturaSwitch;
    arranque::marca("ventana: /switch indexado");

    s64 freeStorage;

    /* Normalmente ya lo hizo precargar() con la pantalla de carga a la vista.
       Si al abrir no habia internet, se reintenta al volver al inicio.
       hayInternet() le pregunta al sistema antes: sin conexion la app se
       quedaba hasta un minuto esperando que vencieran los timeouts. */
    consultarRed();
    const bool conectado = consultado;

    /* Una sola comprobacion por ventana: antes la hacian Estado y Nextendo
       por separado, con una consulta DNS cada una. */
    std::vector<protection::Check> checks;
    if (hayPrecargadas) {
        checks = std::move(precargadas);
        hayPrecargadas = false;
    }
    else {
        checks = protection::runOnConsole();
    }

    std::string tag = conectado ? tagGuardado : "";
    this->latestTag = tag;
    this->setFooterText(fmt::format("menus/main/footer_text"_i18n,
                                    (!tag.empty() && tag != AppVersion) ? AppVersion + "menus/main/new_update"_i18n : AppVersion,
                                    R_SUCCEEDED(fs::getFreeStorageSD(freeStorage)) ? (float)freeStorage / 0x40000000 : -1));

    json hideStatus = fs::parseJsonFile(HIDE_TABS_JSON);
    nlohmann::ordered_json nxlinks;
    if (conectado)
        nxlinks = nxlinksGuardado;
    if (nxlinks.is_object() && nxlinks.contains("enlaces") && nxlinks["enlaces"].is_object()) {
        const auto& enlaces = nxlinks["enlaces"];
        if (enlaces.contains("donar") && enlaces["donar"].is_string() && !enlaces["donar"].get<std::string>().empty())
            this->donationUrl = enlaces["donar"].get<std::string>();
    }

    bool erista = util::isErista();

    if (!util::getBoolValue(hideStatus, "about"))
        this->addTab("menus/main/about"_i18n, new AboutTab(tag, this->donationUrl, checks));
    arranque::marca("ventana: pestaña estado");

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
    arranque::marca("ventana: pestaña packs");

    if (!util::getBoolValue(hideStatus, "firmwares"))
        this->addTab("menus/main/download_firmware"_i18n, new ListDownloadTab(contentType::fw, nxlinks));
    arranque::marca("ventana: pestaña firmwares");

    /* Con Bacán Shop, los trucos van dentro de su seccion: es lo que se usa
       despues de instalar un juego. Sin ella (sin conexion, o el catalogo no
       la trae) siguen en su pestana. */
    const bool conBacan = !util::getBoolValue(hideStatus, "apps") && BacanShopTab::available(nxlinks);
    const bool conTrucos = !util::getBoolValue(hideStatus, "cheats");
    if (conTrucos && !conBacan) {
        this->addTab("menus/main/download_cheats"_i18n, new ListDownloadTab(contentType::cheats));
        arranque::marca("ventana: pestaña trucos");
    }

    if (!util::getBoolValue(hideStatus, "apps"))
    {
        // El numero junto al nombre: lo primero que se ve al abrir la app.
        size_t nuevas = 0;
        const nlohmann::ordered_json instaladas = appUpdates::installed();
        if (nxlinks.is_object() && nxlinks.contains("apps") && nxlinks["apps"].is_object()) {
            for (const auto& url : nxlinks["apps"]) {
                if (url.is_string() && appUpdates::hasUpdate(nxlinks, instaladas, url.get<std::string>()))
                    ++nuevas;
            }
        }
        this->addTab("menus/main/apps"_i18n, new AppsTab(nxlinks));
        arranque::marca("ventana: pestaña apps");
        this->appsSidebar = this->sidebar->getChild(this->sidebar->getViewsCount() - 1);
        if (nuevas)
            this->appsItem = this->appsSidebar;
    }

    /* Nextendo: solo con la consola en modo Nextendo, que es cuando sirve. */
    if (!util::getBoolValue(hideStatus, "apps") && NextendoTab::available(nxlinks)) {
        const bool activo = protection::failedOnlyByNextendo(checks);
        this->addTab("Nextendo", new NextendoTab(nxlinks, activo));
        arranque::marca("ventana: pestaña nextendo");
        this->nextendoFunciona = NextendoTab::working(nxlinks, activo);
        if (!NextendoTab::seen() || this->nextendoFunciona)
            this->nextendoItem = this->sidebar->getChild(this->sidebar->getViewsCount() - 1);
    }

    /* Bacán Shop, debajo de Nextendo. Punto dorado si hay version nueva de la
       tienda o de los trucos, o si no esta instalada y nunca se abrio. */
    if (conBacan) {
        this->addTab("Bacán Shop", new BacanShopTab(nxlinks, checks, conTrucos));
        arranque::marca("ventana: pestaña bacan shop");
        const std::string urlTienda = BacanShopTab::url(nxlinks);
        const bool instalada = appUpdates::isInstalled(nxlinks, urlTienda);
        this->bacanNueva = instalada && appUpdates::hasUpdate(nxlinks, appUpdates::installed(), urlTienda);
        this->bacanTrucos = conTrucos && BacanShopTab::trucosNuevos();
        this->bacanSidebar = this->sidebar->getChild(this->sidebar->getViewsCount() - 1);
        if (this->bacanNueva || this->bacanTrucos || (!instalada && !BacanShopTab::seen()))
            this->bacanItem = this->bacanSidebar;
    }
    this->catalogo = nxlinks;
    this->genInstalaciones = appUpdates::generacionInstalaciones();

    /* Mira los dos grupos anteriores, no solo el de descargas: ocultandolas
       todas, estado y ajustes quedarian pegados sin separador. Cada separador
       comprueba ademas que su propio grupo tenga algo, asi que no salen dos
       seguidos ni uno al final. */
    if ((hasStatus || hasDownloads) && hasSettings)
        this->addSeparator();

    if (!util::getBoolValue(hideStatus, "tools"))
        this->addTab("menus/main/tools"_i18n, new ToolsTab(tag, util::getValueFromKey(nxlinks, "payloads"), erista, hideStatus));
    arranque::marca("ventana: pestaña ajustes");

    if (!util::getBoolValue(hideStatus, "language"))
        this->addTab("menus/tools/language"_i18n, new LanguageTab());
    arranque::marca("ventana: pestaña idioma");

    this->registerAction("", brls::Key::B, [this] { return true; });
}

void MainFrame::recalcularAvisos()
{
    const nlohmann::ordered_json& nxlinks = this->catalogo;
    const nlohmann::ordered_json instaladas = appUpdates::installed();
    if (this->appsSidebar) {
        size_t nuevas = 0;
        if (nxlinks.is_object() && nxlinks.contains("apps") && nxlinks["apps"].is_object()) {
            for (const auto& url : nxlinks["apps"]) {
                if (url.is_string() && appUpdates::hasUpdate(nxlinks, instaladas, url.get<std::string>()))
                    ++nuevas;
            }
        }
        this->appsItem = nuevas ? this->appsSidebar : nullptr;
    }
    if (this->bacanSidebar) {
        const std::string urlTienda = BacanShopTab::url(nxlinks);
        const bool instalada = appUpdates::isInstalled(nxlinks, urlTienda);
        this->bacanNueva = instalada && appUpdates::hasUpdate(nxlinks, instaladas, urlTienda);
        this->bacanItem = (this->bacanNueva || this->bacanTrucos || (!instalada && !BacanShopTab::seen())) ? this->bacanSidebar : nullptr;
    }
}

void MainFrame::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    brls::TabFrame::draw(vg, x, y, width, height, style, ctx);

    // Termino una instalacion: lo que ya esta al dia deja de figurar pendiente.
    if (const int gen = appUpdates::generacionInstalaciones(); gen != this->genInstalaciones) {
        this->genInstalaciones = gen;
        this->recalcularAvisos();
    }

    /* Nextendo, mientras no se haya abierto: un punto violeta que respira
       lento. Otro color y otro ritmo que el aviso verde de las apps, para que
       se lea como una invitacion y no como algo pendiente. */
    /* Encendido y quieto cuando Nextendo esta funcionando: se ve que esta activo. */
    if (this->nextendoItem && (!NextendoTab::seen() || this->nextendoFunciona)) {
        static const auto inicioN = std::chrono::steady_clock::now();
        const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - inicioN).count();
        const float respiro = this->nextendoFunciona ? 1.0f : 0.5f + 0.5f * std::sin(t * 1.4f);
        brls::View* item = this->nextendoItem;
        const float cy = item->getY() + item->getHeight() / 2.0f;
        const float cx = item->getX() + item->getWidth() - style->Sidebar.Item.padding - 16.0f;
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, 9.0f + 3.0f * respiro);
        nvgFillColor(vg, this->a(nvgRGBA(150, 120, 255, (unsigned char)(30 + 40 * respiro))));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, 5.0f);
        nvgFillColor(vg, this->a(nvgRGB(160, 132, 255)));
        nvgFill(vg);
    }

    /* Bacán Shop: dorado, como las particulas de la app PC. Late mientras no
       se haya abierto, haya una version nueva o trucos nuevos. */
    if (this->bacanItem && (this->bacanNueva || this->bacanTrucos || !BacanShopTab::seen())) {
        static const auto inicioB = std::chrono::steady_clock::now();
        const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - inicioB).count();
        const float respiro = 0.5f + 0.5f * std::sin(t * 1.8f);
        brls::View* item = this->bacanItem;
        const float cy = item->getY() + item->getHeight() / 2.0f;
        const float cx = item->getX() + item->getWidth() - style->Sidebar.Item.padding - 16.0f;
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, 9.0f + 3.0f * respiro);
        nvgFillColor(vg, this->a(nvgRGBA(230, 185, 92, (unsigned char)(30 + 40 * respiro))));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, 5.0f);
        nvgFillColor(vg, this->a(nvgRGB(230, 185, 92)));
        nvgFill(vg);
    }

    if (!this->appsItem)
        return;

    /* "Nuevas" a la derecha de Apps, con el mismo halo que pulsa en los packs
       pero en verde. Una palabra corta en todos los idiomas, y si aun asi no
       cabe junto al nombre, solo el punto: nunca se corta. */
    static const auto inicio = std::chrono::steady_clock::now();
    const float fase = std::chrono::duration<float>(std::chrono::steady_clock::now() - inicio).count();
    const float pulso = 0.5f + 0.5f * std::sin(fase * 2.5f);
    const bool oscuro = brls::Application::getThemeVariant() == brls::ThemeVariant::DARK;
    const NVGcolor verde = oscuro ? nvgRGB(52, 213, 131) : nvgRGB(20, 140, 80);

    brls::View* item = this->appsItem;
    const float cy = item->getY() + item->getHeight() / 2.0f;
    const float derecha = item->getX() + item->getWidth() - style->Sidebar.Item.padding - 10.0f;

    nvgFontFaceId(vg, ctx->fontStash->regular);
    nvgFontSize(vg, style->Sidebar.Item.textSize);
    const std::string nombre = "menus/main/apps"_i18n;
    const float finNombre = item->getX() + style->Sidebar.Item.textOffsetX + style->Sidebar.Item.padding + nvgTextBounds(vg, 0, 0, nombre.c_str(), nullptr, nullptr);

    const std::string texto = "menus/apps/pending_short"_i18n;
    nvgFontSize(vg, 15.0f);
    const float anchoTexto = nvgTextBounds(vg, 0, 0, texto.c_str(), nullptr, nullptr);
    const float alto = 26.0f, holgura = 11.0f;
    const float ancho = anchoTexto + holgura * 2.0f;

    if (derecha - ancho > finNombre + 12.0f) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, derecha - ancho, cy - alto / 2.0f, ancho, alto, alto / 2.0f);
        nvgFillColor(vg, this->a(nvgRGBA(verde.r * 255, verde.g * 255, verde.b * 255, (unsigned char)(38.0f + 54.0f * pulso))));
        nvgFill(vg);
        nvgFillColor(vg, this->a(verde));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgBeginPath(vg);
        nvgText(vg, derecha - ancho / 2.0f, cy, texto.c_str(), nullptr);
    }
    else {
        nvgBeginPath(vg);
        nvgCircle(vg, derecha - 6.0f, cy, 6.0f + 2.0f * pulso);
        nvgFillColor(vg, this->a(verde));
        nvgFill(vg);
    }
}
