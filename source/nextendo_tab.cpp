#include <set>
#include "nextendo_tab.hpp"
#include "list_download_tab.hpp"
#include "hora.hpp"

#include <switch.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>

#include "app_updates.hpp"
#include "pagina_oqb.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "instalar_app.hpp"
#include "donar_page.hpp"
#include "pack_list_item.hpp"
#include "confirm_page.hpp"
#include "oqb_theme.hpp"

#include <atomic>
#include <thread>
#include "utils.hpp"
#include "worker_page.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {

    std::string text(const nlohmann::ordered_json& j, const char* key)
    {
        const auto v = j.find(key);
        return v != j.end() && v->is_string() ? v->get<std::string>() : "";
    }

    std::vector<std::string> lista(const nlohmann::ordered_json& j, const char* key)
    {
        std::vector<std::string> out;
        const auto v = j.find(key);
        if (v != j.end() && v->is_array())
            for (const auto& s : *v)
                if (s.is_string())
                    out.push_back(s.get<std::string>());
        return out;
    }

    /* Una ventana con un texto, el QR de un enlace si hay, y botones al final.
       Los botones son lo que recibe el foco: asi la lista baja hasta ellos,
       pasando por todo el texto, sin enmarcar el texto en verde.
       `alResponder`: si esta, los botones son "Ya lo hice" / "Aun no" y avisan
       la respuesta; si no, uno solo para volver. */
    /* Un parrafo que puede tener el foco pero no muestra el marco: asi la lista
       se puede recorrer parrafo a parrafo desde arriba, sin que el texto largo
       quede cortado y sin enmarcarlo en verde. */
    class Parrafo : public brls::Label
    {
    public:
        using brls::Label::Label;

        brls::View* getDefaultFocus() override
        {
            return this;
        }

        // Lo mismo que View::onFocusGained, sin animar el marco.
        void onFocusGained() override
        {
            this->focused = true;
            this->getFocusEvent()->fire(this);
            if (this->hasParent())
                this->getParent()->onChildFocusGained(this);
        }
    };

    void abrirDetalle(const std::string& titulo, const std::string& cuerpo, const std::string& qr = "",
                      std::function<void(bool)> alResponder = nullptr, const std::string& qrTexto = "")
    {
        brls::List* l = new brls::List();
        /* Todo el texto en una sola etiqueta: con una por parrafo, la lista dejaba su
           espacio grande entre cada una y el texto no entraba sin desplazarse. Toma
           el foco, asi la ventana abre arriba. */
        if (!cuerpo.empty()) {
            Parrafo* texto = new Parrafo(brls::LabelStyle::REGULAR, cuerpo, true);
            texto->setFontSize(brls::Application::getStyle()->Label.descriptionFontSize + 2);
            l->addView(texto);
        }
        if (!qr.empty())
            l->addView(new QrFilaView(qr, qrTexto.empty() ? "menus/nextendo/scan_page"_i18n : qrTexto));
        if (alResponder) {
            brls::ListItem* si = new brls::ListItem("menus/nextendo/done_yes"_i18n);
            si->getClickEvent()->subscribe([alResponder](brls::View* view) {
                alResponder(true);
                brls::Application::popView();
            });
            l->addView(si);
            brls::ListItem* no = new brls::ListItem("menus/nextendo/done_no"_i18n);
            no->getClickEvent()->subscribe([alResponder](brls::View* view) {
                alResponder(false);
                brls::Application::popView();
            });
            l->addView(no);
        }
        else {
            brls::ListItem* volver = new brls::ListItem("menus/common/back"_i18n);
            volver->getClickEvent()->subscribe([](brls::View* view) { brls::Application::popView(); });
            l->addView(volver);
        }
        brls::AppletFrame* frame = new brls::AppletFrame(true, true);
        frame->setContentView(l);
        brls::PopupFrame::open(titulo, frame, "", "");
    }

    // Lo que el usuario confirmo del checklist, por titulo.
    constexpr const char CONFIRMADOS_PATH[] = "/config/aio-switch-updater/nextendo_checklist.json";

    bool confirmado(const std::string& titulo)
    {
        const nlohmann::ordered_json j = fs::parseJsonFile(CONFIRMADOS_PATH);
        return j.is_object() && j.contains(titulo) && j[titulo].is_boolean() && j[titulo].get<bool>();
    }

    void guardarConfirmado(const std::string& titulo, bool valor)
    {
        nlohmann::ordered_json j = fs::parseJsonFile(CONFIRMADOS_PATH);
        if (!j.is_object())
            j = nlohmann::ordered_json::object();
        j[titulo] = valor;
        fs::writeJsonToFile(j, CONFIRMADOS_PATH);
    }

    std::string vinetas(const std::vector<std::string>& pasos)
    {
        std::string out;
        for (const auto& p : pasos)
            out += (out.empty() ? "" : "\n") + std::string("•  ") + p;
        return out;
    }

    void abrirError(const nlohmann::ordered_json& e)
    {
        const std::string cuerpo = text(e, "que_es") + "\n\n" + "menus/nextendo/what_to_do"_i18n + "\n" + vinetas(lista(e, "que_hacer"));
        abrirDetalle(text(e, "codigo") + " · " + text(e, "titulo"), cuerpo);
    }

    // "2618 0521", "26180521" y "2618-0521" son el mismo codigo.
    std::string normalizar(const std::string& s)
    {
        std::string out;
        for (unsigned char c : s)
            if (std::isalnum(c))
                out += std::tolower(c);
        return out;
    }

}  // namespace

namespace {
    constexpr const char VISTO_PATH[] = "/config/aio-switch-updater/nextendo_visto";
    int visto = -1;  // -1: todavia no se miro el archivo
}  // namespace

bool NextendoTab::seen()
{
    if (visto < 0) {
        std::error_code ec;
        visto = std::filesystem::exists(VISTO_PATH, ec) ? 1 : 0;
    }
    return visto == 1;
}

bool NextendoTab::working(const nlohmann::ordered_json& nxlinks, bool activo)
{
    if (!activo || !nxlinks.contains("nextendo"))
        return false;
    const nlohmann::ordered_json& datos = nxlinks["nextendo"];

    // Firmware compatible: la misma cuenta que el checklist.
    SetSysFirmwareVersion ver;
    if (R_FAILED(setsysGetFirmwareVersion(&ver)))
        return false;
    const std::vector<std::string> compatibles = lista(datos, "firmwares");
    if (std::find(compatibles.begin(), compatibles.end(), std::string(ver.display_version)) == compatibles.end())
        return false;

    // Prelude instalado y sin version nueva.
    const std::string nombre = text(datos, "prelude");
    if (!nxlinks.contains("apps") || !nxlinks["apps"].is_object() || !nxlinks["apps"].contains(nombre) || !nxlinks["apps"][nombre].is_string())
        return false;
    const std::string url = nxlinks["apps"][nombre].get<std::string>();
    return appUpdates::isInstalled(nxlinks, url) && !appUpdates::hasUpdate(nxlinks, appUpdates::installed(), url);
}

void NextendoTab::willAppear(bool resetState)
{
    if (!seen()) {
        visto = 1;
        std::ofstream(VISTO_PATH).put('1');
    }
    brls::List::willAppear(resetState);
}

namespace {

    // La version del juego instalado (la que muestra el menu HOME), o vacia.
    std::string versionInstalada(const std::string& tidHex)
    {
        const u64 tid = std::strtoull(tidHex.c_str(), nullptr, 16);
        auto control = std::make_unique<NsApplicationControlData>();
        u64 size = 0;
        if (R_FAILED(nsGetApplicationControlData(NsApplicationControlSource_Storage, tid, control.get(), sizeof(NsApplicationControlData), &size)) ||
            size < sizeof(control->nacp))
            return "";
        return std::string(control->nacp.display_version, strnlen(control->nacp.display_version, sizeof(control->nacp.display_version)));
    }

    // Las versiones que pide un "requiere" ("Version del juego 2026.19.0 o 2026.18.0").
    std::vector<std::string> versionesPedidas(const std::string& requiere)
    {
        std::vector<std::string> out;
        std::string actual;
        for (char c : requiere + " ") {
            if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && !actual.empty()))
                actual += c;
            else {
                while (!actual.empty() && actual.back() == '.')
                    actual.pop_back();
                if (actual.find('.') != std::string::npos)
                    out.push_back(actual);
                actual.clear();
            }
        }
        return out;
    }

    // "Tienes la 1.4.0" o "Tienes la 1.3.0; pide la 1.4.0", o vacio si no esta instalado.
    std::string comparacion(const std::string& tid, const std::string& requiere, bool* correcta = nullptr)
    {
        const std::string tengo = versionInstalada(tid);
        if (correcta)
            *correcta = false;
        if (tengo.empty())
            return "";
        const std::vector<std::string> pide = versionesPedidas(requiere);
        const bool ok = pide.empty() || std::find(pide.begin(), pide.end(), tengo) != pide.end();
        if (correcta)
            *correcta = ok;
        if (ok || pide.empty())
            return fmt::format("menus/nextendo/you_have"_i18n, tengo);
        return fmt::format("menus/nextendo/you_have_wrong"_i18n, tengo, pide.front());
    }

    /* Tus juegos compatibles instalados, con la version que tienes y la que
       pide Nextendo. */
    void abrirTusJuegos(const nlohmann::ordered_json& datos, const nlohmann::ordered_json& nxlinks)
    {
        const nlohmann::ordered_json juegos = datos.contains("juegos") ? datos["juegos"] : nlohmann::ordered_json::object();
        brls::List* l = new brls::List();
        l->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/nextendo/your_games_intro"_i18n, true));
        /* Las versiones se ponen al dia en Bacán Shop: se abre directo en su
           Biblioteca (Actualizaciones disponibles). Sin Bacán Shop o con version
           nueva, primero se instala o actualiza. */
        std::string bacan;
        if (nxlinks.contains("apps") && nxlinks["apps"].is_object() && nxlinks["apps"].contains("Bacán Shop") && nxlinks["apps"]["Bacán Shop"].is_string())
            bacan = nxlinks["apps"]["Bacán Shop"].get<std::string>();
        if (!bacan.empty()) {
            PackListItem* ir = new PackListItem("menus/nextendo/revisar_bacan"_i18n, "", "menus/nextendo/revisar_bacan_sub"_i18n);
            ir->getClickEvent()->subscribe([nxlinks, bacan](brls::View* v) { abrirOInstalar(nxlinks, "Bacán Shop", bacan, "--actualizaciones"); });
            l->addView(ir);
        }
        int n = 0;
        for (auto it = juegos.begin(); it != juegos.end(); ++it) {
            if (!it.value().is_object())
                continue;
            const std::string tengo = versionInstalada(it.key());
            if (tengo.empty())
                continue;
            ++n;
            const std::string requiere = text(it.value(), "requiere");
            bool ok = false;
            comparacion(it.key(), requiere, &ok);
            PackListItem* item = new PackListItem(text(it.value(), "nombre"), "",
                                                  requiere.empty() ? "menus/nextendo/no_requirement"_i18n : requiere);
            if (requiere.empty())
                item->setValue(fmt::format("menus/nextendo/version"_i18n, tengo), false, false);
            else if (ok)
                item->setPositiveValue(fmt::format("menus/nextendo/version"_i18n, tengo));
            else
                item->setWarningValue(fmt::format("menus/nextendo/version"_i18n, tengo));
            l->addView(item);
        }
        if (n == 0)
            l->addView(new brls::Label(brls::LabelStyle::REGULAR, "menus/nextendo/your_games_none"_i18n, true));
        brls::AppletFrame* frame = new brls::AppletFrame(true, true);
        frame->setContentView(l);
        brls::PopupFrame::open("menus/nextendo/your_games"_i18n, frame, "", "");
    }

    /* Quien esta jugando ahora: se pregunta a Nextendo al abrirlo (no al
       arrancar la app), igual que hace CardsNX. Su API da solo el ID de cada
       juego; los nombres y el estado vienen del catalogo. */
    void abrirConectados(const nlohmann::ordered_json& datos)
    {
        nlohmann::ordered_json res;
        const std::string url = text(datos, "conectados");
        if (!url.empty())
            download::getRequest(url, res);
        if (!res.is_object() || !res.contains("counts") || !res["counts"].is_object()) {
            brls::Application::notify("menus/nextendo/online_error"_i18n);
            return;
        }
        const nlohmann::ordered_json juegos = datos.contains("juegos") ? datos["juegos"] : nlohmann::ordered_json::object();
        auto nombreDe = [&juegos](const std::string& tid) {
            return juegos.contains(tid) && juegos[tid].is_object() ? text(juegos[tid], "nombre") : "";
        };

        std::vector<std::pair<int, std::string>> jugando;
        int total = 0;
        for (auto it = res["counts"].begin(); it != res["counts"].end(); ++it) {
            if (!it.value().is_number_integer())
                continue;
            std::string tid = it.key();
            std::transform(tid.begin(), tid.end(), tid.begin(), [](unsigned char c) { return std::tolower(c); });
            const int n = it.value().get<int>();
            total += n;
            if (n > 0)
                jugando.emplace_back(n, tid);
        }
        std::sort(jugando.begin(), jugando.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

        brls::List* l = new brls::List();
        l->addView(new HeaderOqb(total == 1 ? "menus/nextendo/online_one"_i18n : fmt::format("menus/nextendo/online_total"_i18n, total)));
        if (jugando.empty())
            l->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/nextendo/online_nobody"_i18n, true));
        for (const auto& [n, tid] : jugando) {
            const std::string nombre = nombreDe(tid);
            const std::string requiere = juegos.contains(tid) && juegos[tid].is_object() ? text(juegos[tid], "requiere") : "";
            const std::string mia = comparacion(tid, requiere);
            std::string sub = requiere;
            if (!mia.empty())
                sub += (sub.empty() ? "" : " · ") + mia;
            PackListItem* item = new PackListItem(nombre.empty() ? tid : nombre, "", sub);
            item->setPositiveValue(n == 1 ? "menus/nextendo/players_one"_i18n : fmt::format("menus/nextendo/players"_i18n, n));
            l->addView(item);
        }

        // Todos los compatibles, con su estado segun la wiki.
        l->addView(new HeaderOqb("menus/nextendo/compatible"_i18n));
        for (auto it = juegos.begin(); it != juegos.end(); ++it) {
            if (!it.value().is_object())
                continue;
            const std::string estado = text(it.value(), "estado"), requiere = text(it.value(), "requiere");
            const std::string mia = comparacion(it.key(), requiere);
            std::string sub = requiere;
            if (!mia.empty())
                sub += (sub.empty() ? "" : " · ") + mia;
            brls::ListItem* item = new brls::ListItem(text(it.value(), "nombre"), "", sub);
            if (!estado.empty())
                item->setValue(estado, true, false);
            l->addView(item);
        }

        brls::AppletFrame* frame = new brls::AppletFrame(true, true);
        frame->setContentView(l);
        brls::PopupFrame::open("menus/nextendo/online"_i18n, frame, "menus/nextendo/online_sub"_i18n, "");
    }

    /* Arriba de Nextendo: el QR para crear la cuenta y cuantos juegan ahora. El
       numero se pide en un hilo (solo datos); mientras llega, no se muestra. */
    class CuentaYJugadores : public brls::View
    {
        QrCodeView* qr;
        std::thread hilo;
        std::atomic<int> jugadores{-1};
        int fuente = -1;
        bool fuenteIntentada = false;

    public:
        CuentaYJugadores(const std::string& urlCuenta, const std::string& urlConectados) : qr(new QrCodeView(urlCuenta))
        {
            this->qr->setParent(this);
            this->setHeight(150);
            if (!urlConectados.empty())
                this->hilo = std::thread([this, urlConectados] {
                    try {
                        std::string texto;
                        if (download::textoCorto(urlConectados, texto, 6, 1u << 20) != 200) return;
                        const auto j = nlohmann::json::parse(texto, nullptr, false);
                        if (!j.is_object() || !j.contains("counts") || !j["counts"].is_object()) return;
                        int total = 0;
                        for (const auto& [k, v] : j["counts"].items())
                            if (v.is_number_integer()) total += v.get<int>();
                        this->jugadores = total;
                    }
                    catch (...) {
                    }
                });
        }
        ~CuentaYJugadores() override
        {
            if (this->hilo.joinable()) this->hilo.join();
            delete this->qr;
        }
        void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override {}
        void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override
        {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, width, height, 16.0f);
            nvgFillColor(vg, this->a(oqb::velo(oqb::oscuro() ? 12 : 10)));
            nvgFill(vg);
            nvgStrokeColor(vg, this->a(ctx->theme->sidebarSeparatorColor));
            nvgStrokeWidth(vg, 1.0f);
            nvgStroke(vg);
            // El QR sobre blanco, para que el celular lo lea en modo oscuro tambien.
            const float lado = height - 28.0f;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x + 16, y + 14, lado, lado, 10.0f);
            nvgFillColor(vg, nvgRGB(255, 255, 255));
            nvgFill(vg);
            this->qr->setBoundaries(x + 22, y + 20, lado - 12, lado - 12);
            this->qr->frame(ctx);
            const float tx = x + 16 + lado + 24;
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgFontFaceId(vg, ctx->fontStash->regular);
            nvgFontSize(vg, 21.0f);
            nvgFillColor(vg, this->a(ctx->theme->textColor));
            nvgBeginPath(vg);
            nvgText(vg, tx, y + 34, "menus/nextendo/cuenta_titulo"_i18n.c_str(), nullptr);
            nvgFontSize(vg, 16.0f);
            nvgTextLineHeight(vg, 1.3f);
            nvgFillColor(vg, this->a(ctx->theme->descriptionColor));
            nvgBeginPath(vg);
            nvgTextBox(vg, tx, y + 66, width - (tx - x) - 220, "menus/nextendo/cuenta_texto"_i18n.c_str(), nullptr);
            const int n = this->jugadores.load();
            if (n >= 0) {
                if (!this->fuenteIntentada) {
                    this->fuenteIntentada = true;
                    this->fuente = nvgFindFont(vg, "upheaval");
                    if (this->fuente < 0) this->fuente = nvgCreateFont(vg, "upheaval", "romfs:/fonts/upheavtt.ttf");
                }
                nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
                nvgFontFaceId(vg, this->fuente >= 0 ? this->fuente : ctx->fontStash->regular);
                nvgFontSize(vg, 44.0f);
                nvgFillColor(vg, this->a(oqb::oscuro() ? nvgRGB(187, 168, 255) : nvgRGB(75, 50, 168)));
                nvgBeginPath(vg);
                nvgText(vg, x + width - 28, y + 36, std::to_string(n).c_str(), nullptr);
                nvgFontFaceId(vg, ctx->fontStash->regular);
                nvgFontSize(vg, 15.0f);
                nvgFillColor(vg, this->a(ctx->theme->descriptionColor));
                nvgBeginPath(vg);
                nvgText(vg, x + width - 28, y + 88, "menus/nextendo/jugando_ahora"_i18n.c_str(), nullptr);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            }
        }
    };

}  // namespace

bool NextendoTab::available(const nlohmann::ordered_json& nxlinks)
{
    return nxlinks.is_object() && nxlinks.contains("nextendo") && nxlinks["nextendo"].is_object();
}

NextendoTab::NextendoTab(const nlohmann::ordered_json& nxlinks, bool activo) : brls::List(), nxlinks(nxlinks), activo(activo)
{
    const nlohmann::ordered_json& datos = this->nxlinks["nextendo"];

    /* Sin el modo activo: primero que es. Debajo, crear la cuenta (con el QR) y
       quien esta jugando; el "Como empezar" sobraba con el checklist y el QR. */
    const std::string presentacion = text(datos, "presentacion");
    if (!activo && !presentacion.empty()) {
        this->addView(new HeaderOqb("menus/nextendo/what_is"_i18n));
        this->addView(new brls::Label(brls::LabelStyle::REGULAR, presentacion, true));
    }
    this->addView(new CuentaYJugadores("https://nextendo.network", text(datos, "conectados")));
    if (!text(datos, "conectados").empty()) {
        brls::ListItem* online = new brls::ListItem("menus/nextendo/online"_i18n, "", "menus/nextendo/online_sub"_i18n);
        const nlohmann::ordered_json copia = datos;
        online->getClickEvent()->subscribe([copia](brls::View* view) { abrirConectados(copia); });
        this->addView(online);
    }
    this->addChecklist(datos);

    const auto etapas = datos.find("etapas");
    if (etapas != datos.end() && etapas->is_array() && !etapas->empty()) {
        this->addView(new HeaderOqb("menus/nextendo/stages"_i18n));
        for (const auto& e : *etapas) {
            if (text(e, "seccion") == "errores")
                continue;
            const std::string titulo = text(e, "titulo");
            const std::string cuerpo = vinetas(lista(e, "pasos"));
            brls::ListItem* item = new brls::ListItem(titulo);
            item->getClickEvent()->subscribe([titulo, cuerpo](brls::View* view) { abrirDetalle(titulo, cuerpo); });
            this->addView(item);
        }
    }
    // Buscar un codigo de error, como una entrada mas de las etapas.
    this->addSearch(datos);

    const auto errores = datos.find("errores");
    if (errores != datos.end() && errores->is_array() && !errores->empty()) {
        this->addView(new HeaderOqb("menus/nextendo/errors"_i18n));
        for (const auto& e : *errores) {
            brls::ListItem* item = new brls::ListItem(text(e, "codigo"), "", text(e, "titulo"));
            const nlohmann::ordered_json copia = e;
            item->getClickEvent()->subscribe([copia](brls::View* view) { abrirError(copia); });
            this->addView(item);
        }
        /* Las etapas que el catalogo manda a esta seccion ("No se que fallo"):
           una tarjeta mas, al lado del ultimo codigo, no una fila sola. */
        if (etapas != datos.end() && etapas->is_array()) {
            for (const auto& e : *etapas) {
                if (text(e, "seccion") != "errores")
                    continue;
                const std::string titulo = text(e, "titulo");
                const std::string cuerpo = vinetas(lista(e, "pasos"));
                brls::ListItem* item = new brls::ListItem(titulo);
                item->getClickEvent()->subscribe([titulo, cuerpo](brls::View* view) { abrirDetalle(titulo, cuerpo); });
                this->addView(item);
            }
        }
    }

    const auto notas = datos.find("notas");
    if (notas != datos.end() && notas->is_array() && !notas->empty()) {
        this->addView(new HeaderOqb("menus/nextendo/notes"_i18n));
        for (const auto& n : *notas) {
            const std::string titulo = text(n, "titulo"), cuerpo = text(n, "texto");
            brls::ListItem* item = new brls::ListItem(titulo);
            item->getClickEvent()->subscribe([titulo, cuerpo](brls::View* view) { abrirDetalle(titulo, cuerpo); });
            this->addView(item);
        }
    }

    this->addView(new HeaderOqb("menus/nextendo/more"_i18n));
    const std::string video = text(datos, "video"), wiki = text(datos, "wiki");
    if (!video.empty()) {
        brls::ListItem* item = new brls::ListItem("menus/nextendo/video"_i18n);
        item->getClickEvent()->subscribe([video](brls::View* view) {
            abrirDetalle("menus/nextendo/video"_i18n, "menus/firmware/tutorial_text"_i18n, video, nullptr, "menus/nextendo/scan_video"_i18n);
        });
        this->addView(item);
    }
    if (!wiki.empty()) {
        brls::ListItem* item = new brls::ListItem("menus/nextendo/wiki"_i18n);
        item->getClickEvent()->subscribe([wiki](brls::View* view) {
            abrirDetalle("menus/nextendo/wiki"_i18n, "menus/nextendo/wiki_text"_i18n, wiki, nullptr, "menus/nextendo/scan_wiki"_i18n);
        });
        this->addView(item);
    }
}

void NextendoTab::addSearch(const nlohmann::ordered_json& datos)
{
    brls::ListItem* buscar = new brls::ListItem("menus/nextendo/search"_i18n);
    const nlohmann::ordered_json errores = datos.contains("errores") ? datos["errores"] : nlohmann::ordered_json::array();
    buscar->getClickEvent()->subscribe([errores](brls::View* view) {
        std::string query;
        if (!brls::Swkbd::openForText([&query](std::string t) { query = t; }, "menus/nextendo/search"_i18n, "", 32, "", 0,
                                      "menus/nextendo/search_ok"_i18n, "2618-0521"))
            return;
        const std::string q = normalizar(query);
        if (q.empty())
            return;
        // Primero por codigo; si no, por palabras del titulo.
        for (const auto& e : errores)
            if (normalizar(text(e, "codigo")).find(q) != std::string::npos) {
                abrirError(e);
                return;
            }
        for (const auto& e : errores)
            if (normalizar(text(e, "titulo")).find(q) != std::string::npos) {
                abrirError(e);
                return;
            }
        brls::Application::notify(fmt::format("menus/nextendo/not_found"_i18n, query));
    });
    this->addView(buscar);
}

void NextendoTab::addChecklist(const nlohmann::ordered_json& datos)
{
    this->addView(new HeaderOqb("menus/nextendo/checklist"_i18n));
    // Lo que el checklist revisa, como subtitulo de la seccion.
    this->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/nextendo/intro"_i18n, true));

    // Firmware: lo dice la consola.
    {
        std::string version;
        SetSysFirmwareVersion ver;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&ver)))
            version = ver.display_version;
        const std::vector<std::string> compatibles = lista(datos, "firmwares");
        const bool ok = std::find(compatibles.begin(), compatibles.end(), version) != compatibles.end();
        std::string lista_txt;
        for (const auto& c : compatibles)
            lista_txt += (lista_txt.empty() ? "" : ", ") + c;
        PackListItem* item = new PackListItem(fmt::format("menus/nextendo/check_firmware"_i18n, version.empty() ? "?" : version), "",
                                              fmt::format("menus/nextendo/check_firmware_sub"_i18n, lista_txt));
        if (ok)
            item->setPositiveValue("menus/nextendo/ok"_i18n);
        else {
            item->setWarningValue("menus/nextendo/check"_i18n);
            // Con uno que no sirve, A lleva a elegir un firmware para actualizar.
            const nlohmann::ordered_json links = this->nxlinks;
            item->getClickEvent()->subscribe([links](brls::View* view) {
                brls::AppletFrame* frame = new brls::AppletFrame(true, true);
                frame->setTitle("menus/main/download_firmware"_i18n);
                frame->setContentView(new ListDownloadTab(contentType::fw, links));
                brls::Application::pushView(frame);
            });
        }
        this->addView(item);
    }

    // Prelude instalado y al dia: lo mismo que la pestana de apps.
    {
        const std::string nombre = text(datos, "prelude");
        std::string url;
        if (this->nxlinks.contains("apps") && this->nxlinks["apps"].is_object() && this->nxlinks["apps"].contains(nombre) && this->nxlinks["apps"][nombre].is_string())
            url = this->nxlinks["apps"][nombre].get<std::string>();
        if (!url.empty()) {
            const bool instalada = appUpdates::isInstalled(this->nxlinks, url);
            const bool nueva = instalada && appUpdates::hasUpdate(this->nxlinks, appUpdates::installed(), url);
            /* Tambien es un atajo: con A se instala o se actualiza ahi mismo. */
            const std::string sub = !instalada ? "menus/nextendo/prelude_install"_i18n
                                    : nueva    ? "menus/nextendo/prelude_update"_i18n
                                               : "menus/nextendo/prelude_reinstall"_i18n;
            PackListItem* item = new PackListItem("menus/nextendo/check_prelude"_i18n, "", sub);
            if (instalada && !nueva)
                item->setPositiveValue("menus/nextendo/ok"_i18n);
            else
                item->setWarningValue(instalada ? "menus/apps/new_version"_i18n : "menus/nextendo/missing"_i18n);
            const nlohmann::ordered_json links = this->nxlinks;
            item->getClickEvent()->subscribe([links, nombre, url](brls::View* view) { instalarAppDelCatalogo(links, nombre, url); });
            this->addView(item);
        }
    }

    // El modo en si: lo que el verificador ya sabe.
    {
        PackListItem* item = new PackListItem("menus/nextendo/check_mode"_i18n, "", "menus/nextendo/check_mode_sub"_i18n);
        if (this->activo)
            item->setPositiveValue("menus/nextendo/active"_i18n);
        else
            item->setWarningValue("menus/nextendo/inactive"_i18n);
        /* El modo se cambia en Prelude: con A se abre directo (cierra esta app). */
        const std::string nombre = text(datos, "prelude");
        if (this->nxlinks.contains("apps") && this->nxlinks["apps"].is_object() && this->nxlinks["apps"].contains(nombre) &&
            this->nxlinks["apps"][nombre].is_string()) {
            const std::string url = this->nxlinks["apps"][nombre].get<std::string>();
            const std::string ruta = appUpdates::rutaParaAbrir(this->nxlinks, url);
            if (!ruta.empty())
                item->getClickEvent()->subscribe([ruta](brls::View* view) { util::abrirAppInstalada(ruta, "Prelude"); });
        }
        this->addView(item);
    }


    // Tus juegos compatibles: si alguno no tiene la version que pide Nextendo.
    {
        const nlohmann::ordered_json juegos = datos.contains("juegos") ? datos["juegos"] : nlohmann::ordered_json::object();
        int instalados = 0, mal = 0;
        /* Primero que juegos hay en la consola (una sola consulta). Pedir el
           control de cada juego del catalogo, este o no instalado, tardaba unos
           3,5 s, y la ventana se rearma tras cada instalacion. */
        std::set<u64> enLaConsola;
        {
            std::vector<NsApplicationRecord> registros(64000);
            s32 cuantos = 0;
            if (R_SUCCEEDED(nsListApplicationRecord(registros.data(), registros.size(), 0, &cuantos)))
                for (s32 i = 0; i < cuantos; ++i) enLaConsola.insert(registros[i].application_id);
        }
        for (auto it = juegos.begin(); it != juegos.end(); ++it) {
            if (!it.value().is_object() || !enLaConsola.count(std::strtoull(it.key().c_str(), nullptr, 16)) || versionInstalada(it.key()).empty())
                continue;
            ++instalados;
            bool ok = false;
            comparacion(it.key(), text(it.value(), "requiere"), &ok);
            if (!ok)
                ++mal;
        }
        PackListItem* item = new PackListItem("menus/nextendo/your_games"_i18n, "", "menus/nextendo/your_games_sub"_i18n);
        if (instalados == 0)
            item->setValue("menus/nextendo/see_more"_i18n, true, false);
        else if (mal == 0)
            item->setPositiveValue("menus/nextendo/ok"_i18n);
        else
            item->setWarningValue(mal == 1 ? "menus/nextendo/wrong_one"_i18n : fmt::format("menus/nextendo/wrong_many"_i18n, mal));
        const nlohmann::ordered_json copia = datos;
        const nlohmann::ordered_json links = this->nxlinks;
        item->getClickEvent()->subscribe([copia, links](brls::View* view) { abrirTusJuegos(copia, links); });
        this->addView(item);
    }

    // Fecha y hora: sin poder compararla con nada, solo se muestra y se avisa si
    // es imposible (anterior a la app). Mal puesta, rompe la conexion segura.
    {
        const std::time_t ahora = std::time(nullptr);
        std::tm local{};
        localtime_r(&ahora, &local);
        char fecha[32];
        std::strftime(fecha, sizeof(fecha), "%d/%m/%Y %H:%M", &local);
        PackListItem* item = new PackListItem("menus/nextendo/check_clock"_i18n, "", "menus/nextendo/check_clock_sub"_i18n);
        if (local.tm_year + 1900 < 2026)
            item->setWarningValue(fecha);
        else
            item->setValue(fecha, false, false);
        /* A: las dos opciones de la hora (antes en Herramientas): sincronizarla
           ahora y el interruptor de sincronizacion por internet de la consola. */
        item->getClickEvent()->subscribe([](brls::View* view) {
            brls::List* l = new brls::List();
            brls::ListItem* ahora = new brls::ListItem("menus/time/sync"_i18n, "", "menus/nextendo/clock_sync_sub"_i18n);
            ahora->getClickEvent()->subscribe([](brls::View* v) { hora::sincronizar(); });
            l->addView(ahora);
            bool activa = false;
            const bool leida = hora::leerAutomatica(activa);
            brls::ListItem* auto_ = new brls::ListItem("menus/time/system_sync"_i18n, "", "menus/nextendo/clock_auto_sub"_i18n);
            auto_->setValue(!leida ? "menus/about/status_unknown"_i18n : activa ? "menus/time/system_sync_on"_i18n : "menus/time/system_sync_off"_i18n);
            auto_->getClickEvent()->subscribe([](brls::View* v) { hora::alternarAutomatica(); });
            l->addView(auto_);
            brls::AppletFrame* frame = new brls::AppletFrame(true, true);
            frame->setContentView(l);
            brls::PopupFrame::open("menus/nextendo/check_clock"_i18n, frame, "", "");
        });
        this->addView(item);
    }

    // Lo que no se puede revisar desde la consola: se abre con sus pasos y su QR.
    const auto checklist = datos.find("checklist");
    if (checklist != datos.end() && checklist->is_array()) {
        for (const auto& c : *checklist) {
            const std::string titulo = text(c, "titulo"), cuerpo = text(c, "texto"), qr = text(c, "qr"), qrTexto = text(c, "qr_texto");
            /* "Confirmado por ti" en texto normal, no en la pildora verde de
               "Listo": lo marca el usuario, no lo comprobo la app. */
            brls::ListItem* item = new brls::ListItem(titulo);
            if (confirmado(titulo))
                item->setValue("menus/nextendo/confirmed"_i18n, false, false);
            else
                item->setValue("menus/nextendo/see_more"_i18n, true, false);
            item->getClickEvent()->subscribe([item, titulo, cuerpo, qr, qrTexto](brls::View* view) {
                abrirDetalle(titulo, cuerpo, qr, [item, titulo](bool hecho) {
                    guardarConfirmado(titulo, hecho);
                    if (hecho)
                        item->setValue("menus/nextendo/confirmed"_i18n, false, false);
                    else
                        item->setValue("menus/nextendo/see_more"_i18n, true, false);
                }, qrTexto);
            });
            this->addView(item);
        }
    }
}
