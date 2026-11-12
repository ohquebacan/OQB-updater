#include "bacan_shop_tab.hpp"

#include <switch.h>

#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "app_updates.hpp"
#include "pagina_oqb.hpp"
#include "fs.hpp"
#include "instalar_app.hpp"
#include "list_download_tab.hpp"
#include "pack_list_item.hpp"
#include "pack_version.hpp"
#include "protection_page.hpp"
#include "trucos_page.hpp"
#include "utils.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {
    constexpr const char NOMBRE[] = "Bacán Shop";
    constexpr const char VISTO_PATH[] = "/config/aio-switch-updater/bacan_visto";
    int visto = -1;  // -1: todavia no se miro el archivo

    // Menos que esto y es probable que un juego no quepa.
    constexpr double ESPACIO_MINIMO_GB = 10.0;

    /* Las imagenes del romfs se crean una vez por sesion: la pestana se vuelve
       a armar cada vez que se vuelve al inicio. */
    int imagen(NVGcontext* vg, const char* ruta)
    {
        static std::map<std::string, int> cache;
        const auto it = cache.find(ruta);
        if (it != cache.end())
            return it->second;
        const int id = nvgCreateImage(vg, ruta, 0);
        cache[ruta] = id;
        return id;
    }

    void dibujarImagen(NVGcontext* vg, int id, float x, float y, float lado, float radio, float alfa)
    {
        if (id <= 0)
            return;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, lado, lado, radio);
        nvgFillPaint(vg, nvgImagePattern(vg, x, y, lado, lado, 0, id, alfa));
        nvgFill(vg);
    }

    /* La ficha de arriba: icono, nombre, una pildora con el estado y una frase
       de que es. No recibe foco: la accion va en la fila de abajo. */
    class Ficha : public brls::View, public ConBoton
    {
    private:
        std::string estado;
        NVGcolor colorEstado;
        bool hayEstado;
        std::string descripcion;

    public:
        Ficha(const std::string& estado, NVGcolor colorEstado, bool hayEstado, const std::string& descripcion)
            : estado(estado), colorEstado(colorEstado), hayEstado(hayEstado), descripcion(descripcion)
        {
            this->setHeight(176);
        }

        void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override
        {
            const float cx = x, cy = y + 14.0f, cw = width, ch = height - 28.0f;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx, cy, cw, ch, 16.0f);
            nvgFillColor(vg, this->a(ctx->theme->highlightBackgroundColor));
            nvgFill(vg);
            nvgStrokeColor(vg, this->a(ctx->theme->sidebarSeparatorColor));
            nvgStrokeWidth(vg, 1.0f);
            nvgStroke(vg);

            const float lado = 104.0f;
            dibujarImagen(vg, imagen(vg, "romfs:/bacan/icono.png"), cx + 26.0f, cy + (ch - lado) / 2.0f, lado, 22.0f, this->alpha);

            const float tx = cx + 26.0f + lado + 26.0f;
            const float ancho = cw - (tx - cx) - 26.0f - this->reservaDerecha;

            nvgFontFaceId(vg, ctx->fontStash->regular);
            nvgFontSize(vg, 32.0f);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFillColor(vg, this->a(ctx->theme->textColor));
            const float yTitulo = cy + 44.0f;
            nvgBeginPath(vg);
            nvgText(vg, tx, yTitulo, NOMBRE, nullptr);
            const float finTitulo = tx + nvgTextBounds(vg, 0, 0, NOMBRE, nullptr, nullptr);

            if (this->hayEstado) {
                nvgFontSize(vg, 16.0f);
                const float anchoP = nvgTextBounds(vg, 0, 0, this->estado.c_str(), nullptr, nullptr) + 24.0f;
                const float px = finTitulo + 16.0f;
                nvgBeginPath(vg);
                nvgRoundedRect(vg, px, yTitulo - 14.0f, anchoP, 28.0f, 14.0f);
                nvgFillColor(vg, this->a(this->colorEstado));
                nvgFill(vg);
                nvgFillColor(vg, this->a(nvgRGB(10, 20, 8)));
                nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                nvgBeginPath(vg);
                nvgText(vg, px + anchoP / 2.0f, yTitulo + 1.0f, this->estado.c_str(), nullptr);
            }

            nvgFontSize(vg, 19.0f);
            nvgTextLineHeight(vg, 1.35f);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgFillColor(vg, this->a(ctx->theme->descriptionColor));
            nvgBeginPath(vg);
            nvgTextBox(vg, tx, yTitulo + 26.0f, ancho, this->descripcion.c_str(), nullptr);
        }

        void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override {}

        // Al terminar una instalacion: la pildora pasa a "Al dia" sin rearmar la pestana.
        void cambiarEstado(const std::string& nuevo, NVGcolor color)
        {
            this->estado = nuevo;
            this->colorEstado = color;
            this->hayEstado = true;
        }
    };

    /* Sin instalar: tres fichas con lo que trae la tienda. */
    class QueTrae : public brls::View
    {
    public:
        QueTrae()
        {
            this->setHeight(214);
        }

        void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override
        {
            struct Ficha {
                const char* icono;
                std::string titulo, texto;
            };
            const Ficha fichas[3] = {
                {"romfs:/bacan/tienda.png", "menus/bacan/t1"_i18n, "menus/bacan/t1_sub"_i18n},
                {"romfs:/bacan/descargas.png", "menus/bacan/t2"_i18n, "menus/bacan/t2_sub"_i18n},
                {"romfs:/bacan/retro.png", "menus/bacan/t3"_i18n, "menus/bacan/t3_sub"_i18n},
            };
            const float hueco = 18.0f;
            const float w = (width - hueco * 2.0f) / 3.0f;
            const float top = y + 10.0f, alto = height - 20.0f;
            for (int i = 0; i < 3; ++i) {
                const float fx = x + i * (w + hueco);
                nvgBeginPath(vg);
                nvgRoundedRect(vg, fx, top, w, alto, 14.0f);
                nvgFillColor(vg, this->a(ctx->theme->highlightBackgroundColor));
                nvgFill(vg);
                nvgStrokeColor(vg, this->a(ctx->theme->sidebarSeparatorColor));
                nvgStrokeWidth(vg, 1.0f);
                nvgStroke(vg);

                dibujarImagen(vg, imagen(vg, fichas[i].icono), fx + 20.0f, top + 18.0f, 64.0f, 0.0f, this->alpha);

                nvgFontFaceId(vg, ctx->fontStash->regular);
                nvgFontSize(vg, 20.0f);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                nvgFillColor(vg, this->a(ctx->theme->textColor));
                nvgBeginPath(vg);
                nvgText(vg, fx + 20.0f, top + 96.0f, fichas[i].titulo.c_str(), nullptr);

                nvgFontSize(vg, 16.0f);
                nvgTextLineHeight(vg, 1.35f);
                nvgFillColor(vg, this->a(ctx->theme->descriptionColor));
                nvgBeginPath(vg);
                nvgTextBox(vg, fx + 20.0f, top + 126.0f, w - 40.0f, fichas[i].texto.c_str(), nullptr);
            }
        }

        void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override {}
    };

    std::string gb(double valor)
    {
        return fmt::format("{:.1f} GB", valor);
    }

    /* El enlace de una app del catalogo, en la seccion de apps que este. */
    std::string enlace(const nlohmann::ordered_json& nxlinks, const std::string& nombre)
    {
        if (!nxlinks.is_object())
            return "";
        for (auto s = nxlinks.begin(); s != nxlinks.end(); ++s) {
            if (s.key().rfind("apps", 0) != 0 || !s.value().is_object())
                continue;
            // apps_descripciones tambien tiene las apps como clave: solo enlaces.
            const auto it = s.value().find(nombre);
            if (it != s.value().end() && it->is_string() && it->get<std::string>().rfind("http", 0) == 0)
                return it->get<std::string>();
        }
        return "";
    }

    std::string sinEspacios(std::string texto)
    {
        const auto fin = texto.find_last_not_of(" \t\r\n");
        texto.erase(fin == std::string::npos ? 0 : fin + 1);
        return texto;
    }

    /* La version de trucos publicada y la descargada. Vacia la publicada sin
       conexion; vacia la descargada si nunca se bajaron. */
    std::pair<std::string, std::string> versionesTrucos()
    {
        // Ya la pidio la precarga del arranque: no toca la red aqui.
        util::revisarBaseTrucos();
        return {sinEspacios(util::getCheatsVersion()), sinEspacios(util::readFile(CHEATS_VERSION))};
    }

    /* Trucos: la lista que antes tenia su propia pestana, en una ventana. Va
       aqui porque es lo que se usa despues de instalar un juego. */
    void agregarTrucos(brls::List* lista)
    {
        lista->addView(new HeaderOqb("menus/bacan/trucos_partidas"_i18n));
        const auto [nueva, tengo] = versionesTrucos();
        PackListItem* item = new PackListItem("menus/bacan/trucos_item"_i18n, "", "menus/bacan/trucos_sub"_i18n);
        if (tengo.empty())
            item->setWarningValue("menus/bacan/trucos_sin"_i18n);
        else if (!nueva.empty() && nueva != tengo)
            item->setWarningValue(fmt::format("menus/bacan/trucos_nuevos"_i18n, nueva));
        else
            item->setPositiveValue("menus/bacan/trucos_al_dia"_i18n);
        // La pantalla de Trucos: cada juego con su version y si tiene trucos para
        // ella. Bajar o administrar la base queda en esa pantalla, con X.
        item->getClickEvent()->subscribe([](brls::View* view) { trucos::abrir(); });
        lista->addView(item);
    }

    /* Partidas completas (partidas.json del catalogo, en nxlinks como
       "_partidas"): cuantas hay para los juegos instalados. Se restauran con
       JKSV BACAN, que ya sabe elegir el usuario y respalda la partida actual
       antes; aqui solo se avisa y, si falta, se instala JKSV. Sin partidas
       para ningun juego instalado no se muestra nada. */
    void agregarPartidas(brls::List* lista, const nlohmann::ordered_json& nxlinks, bool conTitulo)
    {
        if (!nxlinks.is_object())
            return;
        const auto raiz = nxlinks.find("_partidas");
        if (raiz == nxlinks.end() || !raiz->is_object() || !raiz->contains("partidas") || !(*raiz)["partidas"].is_array())
            return;

        // Solo los IDs de lo instalado: sin leer el NACP de cada juego, que es lo lento.
        std::set<u64> instalados;
        {
            constexpr s32 MAXIMO = 64000;
            std::vector<NsApplicationRecord> registros(MAXIMO);
            s32 total = 0;
            if (R_SUCCEEDED(nsListApplicationRecord(registros.data(), MAXIMO, 0, &total))) {
                for (s32 i = 0; i < total; ++i)
                    instalados.insert(registros[i].application_id);
            }
        }

        std::set<u64> conPartida;
        std::vector<std::string> nombres;
        for (const auto& p : (*raiz)["partidas"]) {
            if (!p.is_object() || !p.contains("id") || !p["id"].is_string())
                continue;
            const u64 id = std::strtoull(p["id"].get<std::string>().c_str(), nullptr, 16);
            if (!instalados.count(id) || !conPartida.insert(id).second)
                continue;
            if (p.contains("juego") && p["juego"].is_string())
                nombres.push_back(p["juego"].get<std::string>());
        }
        if (conPartida.empty())
            return;

        std::string juegos;
        for (size_t i = 0; i < nombres.size() && i < 3; ++i)
            juegos += (i ? ", " : "") + nombres[i];
        if (nombres.size() > 3)
            juegos += "…";

        const std::string jksv = enlace(nxlinks, "JKSV BACAN");
        const bool conJksv = !jksv.empty() && appUpdates::isInstalled(nxlinks, jksv);
        std::string sub = juegos.empty() ? "" : juegos + ". ";
        sub += conJksv ? "menus/bacan/partidas_sub"_i18n : "menus/bacan/partidas_sin_jksv_sub"_i18n;

        // Junto a los trucos (misma seccion, lado a lado); sin trucos, con su titulo.
        if (conTitulo) lista->addView(new HeaderOqb("menus/bacan/trucos_partidas"_i18n));
        PackListItem* item = new PackListItem(
            conPartida.size() == 1 ? "menus/bacan/partidas_item_uno"_i18n : fmt::format("menus/bacan/partidas_item"_i18n, conPartida.size()), "", sub);
        if (conJksv) {
            item->setPositiveValue("JKSV BACAN");
            // Se restauran desde JKSV BACAN: al elegirla, como hacerlo.
            const std::string ruta = appUpdates::rutaParaAbrir(nxlinks, jksv);
            item->getClickEvent()->subscribe([ruta](brls::View* view) {
                /* En una ventana con su titulo y letra de lectura: en un dialogo de
                   borealis, con su letra grande, el texto no entraba. */
                brls::List* l = new brls::List();
                // Lo primero: abrirla directo (cierra esta app y abre JKSV BACAN).
                brls::ListItem* abrir = new brls::ListItem("menus/bacan/abrir_jksv"_i18n);
                abrir->getClickEvent()->subscribe([ruta](brls::View* v) { util::abrirAppInstalada(ruta, "JKSV BACAN"); });
                l->addView(abrir);
                brls::Label* texto = new brls::Label(brls::LabelStyle::REGULAR, "menus/bacan/partidas_como"_i18n, true);
                texto->setFontSize(brls::Application::getStyle()->Label.descriptionFontSize + 2);
                l->addView(texto);
                brls::ListItem* volver = new brls::ListItem("menus/common/back"_i18n);
                volver->getClickEvent()->subscribe([](brls::View* v) { brls::Application::popView(); });
                l->addView(volver);
                brls::AppletFrame* frame = new brls::AppletFrame(true, true);
                frame->setContentView(l);
                brls::PopupFrame::open("menus/bacan/partidas"_i18n, frame, "", "");
            });
        }
        else if (!jksv.empty()) {
            item->setWarningValue("menus/bacan/instalar_jksv"_i18n);
            const nlohmann::ordered_json links = nxlinks;
            item->getClickEvent()->subscribe([links, jksv](brls::View* view) { instalarAppDelCatalogo(links, "JKSV BACAN", jksv); });
        }
        lista->addView(item);
    }

    /* Apps que mejoran la experiencia al jugar. La lista sale del catalogo
       ("bacan_sugeridas" en nx-links.json, un arreglo de nombres) para poder
       cambiarla sin sacar otra version; si no la trae, estas. */
    // Una app sugerida, con su icono para la tarjeta de PaginaOqb.
    class ItemConIcono : public PackListItem, public ConIcono
    {
    public:
        using PackListItem::PackListItem;
    };

    constexpr const char* SUGERIDAS[] = {"JKSV BACAN", "SimpleModDownloader", "SimpleModManager", "SaltyNX", "ARCropolis y Skyline"};

    void agregarSugerencias(BacanShopTab* lista, const nlohmann::ordered_json& nxlinks)
    {
        std::vector<std::string> nombres;
        const auto propia = nxlinks.is_object() ? nxlinks.find("bacan_sugeridas") : nxlinks.end();
        if (nxlinks.is_object() && propia != nxlinks.end() && propia->is_array()) {
            for (const auto& n : *propia) {
                if (n.is_string())
                    nombres.push_back(n.get<std::string>());
            }
        }
        else {
            nombres.assign(std::begin(SUGERIDAS), std::end(SUGERIDAS));
        }

        const nlohmann::ordered_json instaladas = appUpdates::installed();
        const auto descs = nxlinks.is_object() ? nxlinks.find("apps_descripciones") : nxlinks.end();
        bool conTitulo = false;
        for (const auto& nombre : nombres) {
            const std::string direccion = enlace(nxlinks, nombre);
            if (direccion.empty())
                continue;
            std::string texto;
            if (nxlinks.is_object() && descs != nxlinks.end() && descs->is_object()) {
                const auto d = descs->find(nombre);
                if (d != descs->end() && d->is_string())
                    texto = d->get<std::string>();
            }
            ItemConIcono* item = new ItemConIcono(nombre, "", texto);
            item->iconoRuta = appUpdates::rutaIcono(nxlinks, direccion);
            if (item->iconoRuta.empty()) {
                item->iconoBytes = appUpdates::iconoInstalado(nxlinks, direccion);
                if (item->iconoBytes.empty()) item->iconoRuta = "romfs:/iconos/generico.png";
            }
            /* Sin la app: A la instala. Con version nueva: A la actualiza. Al dia:
               A la abre, si tiene un .nro que abrir (los sysmodules y plugins no:
               ahi A la reinstala, como antes). */
            const nlohmann::ordered_json links = nxlinks;
            const bool instalada = appUpdates::isInstalled(nxlinks, direccion);
            const bool nueva = instalada && appUpdates::hasUpdate(nxlinks, instaladas, direccion);
            const std::string ruta = instalada && !nueva ? appUpdates::rutaParaAbrir(nxlinks, direccion) : "";
            if (!instalada)
                item->setValue("menus/bacan/instalar_valor"_i18n, false, false);
            else if (nueva)
                item->setWarningValue("menus/apps/new_version"_i18n);
            else
                item->setValue(ruta.empty() ? "menus/apps/installed"_i18n : "menus/bacan/abrir_valor"_i18n, true, false);
            // Se mira al tocar: tras instalarla desde aqui mismo, A ya la abre.
            item->getClickEvent()->subscribe([links, nombre, direccion](brls::View* view) {
                const bool ya = appUpdates::isInstalled(links, direccion) && !appUpdates::hasUpdate(links, appUpdates::installed(), direccion);
                const std::string abrir = ya ? appUpdates::rutaParaAbrir(links, direccion) : "";
                if (!abrir.empty())
                    util::abrirAppInstalada(abrir, nombre);
                else
                    instalarAppDelCatalogo(links, nombre, direccion);
            });
            lista->sugeridas.emplace_back(item, direccion);
            if (!conTitulo) {
                lista->addView(new HeaderOqb("menus/bacan/mejora"_i18n));
                conTitulo = true;
            }
            lista->addView(item);
        }
    }
}  // namespace

bool BacanShopTab::available(const nlohmann::ordered_json& nxlinks)
{
    return !url(nxlinks).empty();
}

std::string BacanShopTab::url(const nlohmann::ordered_json& nxlinks)
{
    /* La tienda esta en "apps_instaladores", no en "apps": se busca en todas
       las secciones de apps para no depender de donde caiga. */
    return enlace(nxlinks, NOMBRE);
}

bool BacanShopTab::trucosNuevos()
{
    // Solo si ya se bajaron alguna vez: a quien nunca los uso no se le insiste.
    const auto [nueva, tengo] = versionesTrucos();
    return !tengo.empty() && !nueva.empty() && nueva != tengo;
}

bool BacanShopTab::seen()
{
    if (visto < 0) {
        std::error_code ec;
        visto = std::filesystem::exists(VISTO_PATH, ec) ? 1 : 0;
    }
    return visto == 1;
}

void BacanShopTab::willAppear(bool resetState)
{
    if (!seen()) {
        visto = 1;
        std::ofstream(VISTO_PATH).put('1');
    }
    brls::List::willAppear(resetState);
}

void BacanShopTab::refrescarEstado()
{
    const bool instalada = appUpdates::isInstalled(this->links, this->direccion);
    if (!instalada || !this->ficha || !this->accion)
        return;
    const bool nueva = appUpdates::hasUpdate(this->links, appUpdates::installed(), this->direccion);
    const std::string tengo = appUpdates::installedVersion(this->direccion);
    const std::string publicada = appUpdates::publishedVersion(this->links, this->direccion);
    const std::string estado = nueva ? fmt::format("menus/bacan/nueva"_i18n, publicada.empty() ? "?" : publicada)
                                     : (tengo.empty() ? "menus/bacan/al_dia_sin_version"_i18n : fmt::format("menus/bacan/al_dia"_i18n, tengo));
    static_cast<Ficha*>(this->ficha)->cambiarEstado(estado, nueva ? nvgRGB(230, 185, 92) : nvgRGB(157, 234, 154));
    if (nueva) {
        this->accion->setLabel("menus/bacan/actualizar"_i18n);
        this->accion->setSubLabel(fmt::format("menus/bacan/actualizar_sub"_i18n, tengo.empty() ? "?" : tengo, publicada.empty() ? "?" : publicada));
        this->accion->setWarningValue("menus/apps/new_version"_i18n);
    }
    else {
        this->accion->setLabel("menus/bacan/reinstalar"_i18n);
        this->accion->setSubLabel("menus/bacan/reinstalar_sub"_i18n);
        this->accion->clearValue();
    }
}

void BacanShopTab::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    // Termino una instalacion (esta tienda o una app sugerida): se vuelve a mirar.
    if (const int gen = appUpdates::generacionInstalaciones(); gen != this->genInstalaciones) {
        this->genInstalaciones = gen;
        this->refrescarEstado();
        for (auto& [item, u] : this->sugeridas) {
            if (!appUpdates::isInstalled(this->links, u))
                continue;
            if (appUpdates::hasUpdate(this->links, appUpdates::installed(), u))
                item->setWarningValue("menus/apps/new_version"_i18n);
            else {
                item->clearValue();
                item->setValue(appUpdates::rutaParaAbrir(this->links, u).empty() ? "menus/apps/installed"_i18n : "menus/bacan/abrir_valor"_i18n, true, false);
            }
        }
    }
    brls::List::draw(vg, x, y, width, height, style, ctx);
}

BacanShopTab::BacanShopTab(const nlohmann::ordered_json& nxlinks, const std::vector<protection::Check>& checks, bool conTrucos) : brls::List()
{
    const std::string direccion = url(nxlinks);
    this->links = nxlinks;
    this->direccion = direccion;
    this->genInstalaciones = appUpdates::generacionInstalaciones();
    const bool instalada = appUpdates::isInstalled(nxlinks, direccion);
    const bool nueva = instalada && appUpdates::hasUpdate(nxlinks, appUpdates::installed(), direccion);
    const std::string tengo = appUpdates::installedVersion(direccion);
    const std::string publicada = appUpdates::publishedVersion(nxlinks, direccion);

    const NVGcolor verde = nvgRGB(157, 234, 154);
    const NVGcolor dorado = nvgRGB(230, 185, 92);

    // La ficha y la accion principal.
    if (!instalada) {
        Ficha* fichaSin = new Ficha("menus/bacan/no_instalada"_i18n, dorado, true, "menus/bacan/descripcion"_i18n);
        this->ficha = fichaSin;
        this->addView(fichaSin);
        PackListItem* instalar = new PackListItem("menus/bacan/instalar"_i18n, "", "menus/bacan/instalar_sub"_i18n);
        instalar->setWarningValue("menus/bacan/instalar_valor"_i18n);
        const nlohmann::ordered_json links = nxlinks;
        instalar->getClickEvent()->subscribe([links, direccion](brls::View* view) { instalarAppDelCatalogo(links, NOMBRE, direccion); });
        this->addView(instalar);
        this->accion = instalar;

        this->addView(new HeaderOqb("menus/bacan/que_trae"_i18n));
        this->addView(new QueTrae());
        if (conTrucos)
            agregarTrucos(this);
        agregarPartidas(this, nxlinks, !conTrucos);
        return;
    }

    const std::string estado = nueva ? fmt::format("menus/bacan/nueva"_i18n, publicada.empty() ? "?" : publicada)
                                     : (tengo.empty() ? "menus/bacan/al_dia_sin_version"_i18n : fmt::format("menus/bacan/al_dia"_i18n, tengo));
    {
        Ficha* fichaCon = new Ficha(estado, nueva ? dorado : verde, true, "menus/bacan/descripcion"_i18n);
        this->ficha = fichaCon;
        this->addView(fichaCon);
    }

    {
        /* Instalada: el boton de la ficha es abrirla (cierra esta app y abre la
           tienda). Reinstalar o actualizar va en la fila de abajo. */
        PackListItem* abrir = new PackListItem("menus/bacan/abrir"_i18n, "", "menus/bacan/abrir_sub"_i18n);
        const std::string ruta = appUpdates::rutaParaAbrir(nxlinks, direccion);
        abrir->getClickEvent()->subscribe([ruta](brls::View* view) { util::abrirAppInstalada(ruta, NOMBRE); });
        this->addView(abrir);
    }

    {
        const nlohmann::ordered_json links = nxlinks;
        PackListItem* accion;
        if (nueva) {
            PackListItem* item = new PackListItem("menus/bacan/actualizar"_i18n, "",
                                                  fmt::format("menus/bacan/actualizar_sub"_i18n, tengo.empty() ? "?" : tengo, publicada.empty() ? "?" : publicada));
            item->setWarningValue("menus/apps/new_version"_i18n);
            accion = item;
        }
        else {
            accion = new PackListItem("menus/bacan/reinstalar"_i18n, "", "menus/bacan/reinstalar_sub"_i18n);
        }
        accion->getClickEvent()->subscribe([links, direccion](brls::View* view) { instalarAppDelCatalogo(links, NOMBRE, direccion); });
        this->addView(accion);
        this->accion = accion;
    }

    // Listo para instalar juegos: lo que hace falta para que lo que se baje abra.
    std::vector<PackListItem*> filas;
    int listos = 0;

    {
        std::error_code ec;
        const bool firmas = !packVersion::current().empty() ||
                            std::filesystem::exists("/atmosphere/exefs_patches/es_patches", ec) ||
                            std::filesystem::exists("/atmosphere/kip_patches/fs_patches", ec);
        PackListItem* item = new PackListItem("menus/bacan/firmas"_i18n, "", firmas ? "menus/bacan/firmas_ok_sub"_i18n : "menus/bacan/firmas_mal_sub"_i18n);
        if (firmas) {
            item->setPositiveValue("menus/bacan/incluidas"_i18n);
            ++listos;
        }
        else
            item->setWarningValue("menus/bacan/faltan"_i18n);
        filas.push_back(item);
    }

    {
        u64 tipo = 0;
        const bool emunand = R_SUCCEEDED(splGetConfig(static_cast<SplConfigItem>(65007), &tipo)) && tipo != 0;
        PackListItem* item = new PackListItem("menus/bacan/emunand"_i18n, "", emunand ? "menus/bacan/emunand_ok_sub"_i18n : "menus/bacan/emunand_mal_sub"_i18n);
        if (emunand) {
            item->setPositiveValue("menus/bacan/activa"_i18n);
            ++listos;
        }
        else
            item->setWarningValue("menus/bacan/no_activa"_i18n);
        filas.push_back(item);
    }

    {
        const bool nextendo = protection::failedOnlyByNextendo(checks);
        const bool dns = protection::dnsBlockingOk(checks);
        PackListItem* item = new PackListItem("menus/bacan/dns"_i18n, "",
                                              dns ? "menus/bacan/dns_ok_sub"_i18n : nextendo ? "menus/bacan/dns_nextendo_sub"_i18n : "menus/bacan/dns_mal_sub"_i18n);
        if (dns) {
            item->setPositiveValue("menus/bacan/activo"_i18n);
            ++listos;
        }
        else
            item->setWarningValue(nextendo ? "menus/bacan/apagado_nextendo"_i18n : "menus/bacan/revisar"_i18n);
        item->getClickEvent()->subscribe([](brls::View* view) {
            brls::PopupFrame::open("menus/tools/protection"_i18n, new ProtectionPage(), "menus/tools/protection_desc"_i18n, "");
        });
        filas.push_back(item);
    }

    {
        s64 libre = 0;
        const bool leido = R_SUCCEEDED(fs::getFreeStorageSD(libre));
        const double libreGb = leido ? (double)libre / 0x40000000 : 0.0;
        const bool alcanza = leido && libreGb >= ESPACIO_MINIMO_GB;
        PackListItem* item = new PackListItem("menus/bacan/espacio"_i18n, "", alcanza ? "menus/bacan/espacio_sub"_i18n : "menus/bacan/espacio_poco_sub"_i18n);
        if (alcanza) {
            item->setPositiveValue(gb(libreGb));
            ++listos;
        }
        else
            item->setWarningValue(leido ? gb(libreGb) : "?");
        filas.push_back(item);
    }

    /* Rediseno: firmas, emuNAND, bloqueo y espacio ya se ven en Inicio; aca no
       se repiten. */
    (void)listos;
    for (PackListItem* f : filas)
        delete f;

    if (conTrucos)
        agregarTrucos(this);
    agregarPartidas(this, nxlinks, !conTrucos);
    agregarSugerencias(this, nxlinks);
}
