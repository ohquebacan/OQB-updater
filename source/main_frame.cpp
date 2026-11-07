#include "main_frame.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <json.hpp>
#include <thread>

#include "about_tab.hpp"
#include "ams_tab.hpp"
#include "app_update.hpp"
#include "app_updates.hpp"
#include "apps_tab.hpp"
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

    // Estado de la consulta de red, compartido por todas las MainFrame de la
    // sesion (la ventana se reconstruye al volver al inicio). Los datos los
    // escribe el hilo; la reconstruccion se decide en draw(), del hilo de
    // dibujo, con el flag rojo como barrera.
    bool consultado = false;
    std::string tagGuardado;
    nlohmann::ordered_json nxlinksGuardado;
    std::atomic<bool> hiloLanzado {false};
    std::atomic<bool> redLista {false};
    bool reconstruido = false;  // una sola reconstruccion automatica por sesion
}  // namespace

MainFrame::MainFrame() : TabFrame()
{
    this->setIcon("romfs:/gui_icon.png");
    this->setTitle(AppTitle);

    s64 freeStorage;

    /* Las consultas de red (tag, nx-links, catalogo) corren en un hilo:
       antes eran sincronas aqui y la app arrancaba con pantalla negra
       varios segundos esperando a los servidores. El primer dibujo sale al
       instante con lo que haya offline; cuando el hilo termina, draw()
       reconstruye la ventana con los datos. */
    if (!consultado && !hiloLanzado.exchange(true)) {
        std::thread([] {
            if (util::hayInternet()) {
                tagGuardado = util::getLatestTag(TAGS_INFO);
                download::getRequest(NXLINKS_URL, nxlinksGuardado);
                if (nxlinksGuardado.is_object())
                    nxlinksGuardado[appUpdates::NXLINKS_KEY] = appUpdates::fetchPublished();
            }
            consultado = nxlinksGuardado.is_object() && !nxlinksGuardado.empty();
            redLista = true;
        }).detach();
    }

    const bool conectado = consultado;

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
        this->addTab("menus/main/about"_i18n, new AboutTab(tag, this->donationUrl));

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
        if (nuevas)
            this->appsItem = this->sidebar->getChild(this->sidebar->getViewsCount() - 1);
    }

    /* Nextendo: solo con la consola en modo Nextendo, que es cuando sirve. */
    if (!util::getBoolValue(hideStatus, "apps") && NextendoTab::available(nxlinks)) {
        const bool activo = protection::failedOnlyByNextendo(protection::runOnConsole());
        this->addTab("Nextendo", new NextendoTab(nxlinks, activo));
        this->nextendoFunciona = NextendoTab::working(nxlinks, activo);
        if (!NextendoTab::seen() || this->nextendoFunciona)
            this->nextendoItem = this->sidebar->getChild(this->sidebar->getViewsCount() - 1);
    }

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

void MainFrame::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    brls::TabFrame::draw(vg, x, y, width, height, style, ctx);

    /* La red llego despues del primer dibujo: se reemplaza la ventana por una
       armada con los datos (una sola vez por sesion, y solo si el usuario
       sigue en el inicio con esta ventana arriba: si abrio otra cosa, se
       respetará y la proxima vez que vuelva al inicio ya vera los datos). */
    /* Ojo: draw() solo se ejecuta mientras esta ventana se esta dibujando,
       o sea que llegar aqui ya dice que el usuario sigue en el inicio (si
       hubiera otra vista encima, esta no dibujaria). Por eso no hace falta
       preguntarle a la pila de vistas. */
    if (redLista && consultado && !reconstruido) {
        reconstruido = true;
        MainFrame* nuevo = new MainFrame();
        brls::Application::pushView(nuevo);
        appUpdate::showNoticeIfAvailable(nuevo->getLatestTag());
        return;
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
