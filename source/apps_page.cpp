#include "apps_page.hpp"

#include <algorithm>
#include <cmath>

#include "app_updates.hpp"
#include "confirm_page.hpp"
#include "led.hpp"
#include "list_download_tab.hpp"
#include "oqb_theme.hpp"
#include "ports_page.hpp"
#include "utils.hpp"
#include "worker_page.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {

    struct Seccion {
        const char* clave;
        const char* etiqueta;
    };
    constexpr Seccion SECCIONES[] = {
        {"apps_emuladores", "menus/apps/sec_emuladores"}, {"apps_mods", "menus/apps/sec_mods"},       {"apps_sysmodules", "menus/apps/sec_sysmodules"},
        {"apps_instaladores", "menus/apps/sec_instaladores"}, {"apps_sistema", "menus/apps/sec_sistema"},
        {"apps_ports", "menus/apps/sec_ports"},
    };
    /* Secciones que no van en «Todas las aplicaciones» ni en el buscador: los
       ports son más de cien y ahí solo harían ruido. Se ven en su chip. */
    constexpr const char* SOLO_EN_SU_SECCION[] = {"apps_ports"};

    std::string cortar(NVGcontext* vg, const std::string& texto, float ancho)
    {
        if (ancho <= 0) return "";
        if (nvgTextBounds(vg, 0, 0, texto.c_str(), nullptr, nullptr) <= ancho) return texto;
        std::string s = texto;
        while (!s.empty()) {
            do s.pop_back();
            while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80);
            if (!s.empty()) s.pop_back();
            const std::string prueba = s + "…";
            if (nvgTextBounds(vg, 0, 0, prueba.c_str(), nullptr, nullptr) <= ancho) return prueba;
        }
        return "…";
    }

    std::string iniciales(const std::string& n)
    {
        std::string out;
        for (size_t i = 0; i < n.size() && out.size() < 2; ++i)
            if (std::isalnum(static_cast<unsigned char>(n[i])) && (i == 0 || n[i - 1] == ' ' || n[i - 1] == '-')) out += std::toupper(static_cast<unsigned char>(n[i]));
        return out.empty() ? "?" : out;
    }

}  // namespace

AppsPage::AppsPage(const nlohmann::ordered_json& nxlinks, const nlohmann::ordered_json& manifiesto) : nxlinks(nxlinks), manifiestoPack(manifiesto)
{
    // Categoria de cada app: la primera seccion que la nombra.
    std::map<std::string, std::string> categoriaDe;
    /* Con la pestana Ports, los ports ya no van en Apps. */
    const bool conPestanaPorts = ports::disponible(nxlinks);
    for (const auto& s : SECCIONES) {
        if (conPestanaPorts && std::string(s.clave) == "apps_ports") continue;
        const auto sec = nxlinks.find(s.clave);
        if (sec == nxlinks.end() || !sec->is_object()) continue;
        for (const auto& [nombre, url] : sec->items()) {
            this->porSeccion[s.clave].push_back(nombre);
            if (!categoriaDe.count(nombre)) categoriaDe[nombre] = i18n::getStr(s.etiqueta);
        }
    }
    const auto descs = nxlinks.find("apps_descripciones");
    const auto todas = nxlinks.find("apps");
    if (todas != nxlinks.end() && todas->is_object()) {
        for (const auto& [nombre, url] : todas->items()) {
            if (!url.is_string()) continue;
            App a;
            a.nombre = nombre;
            a.url = url.get<std::string>();
            a.categoria = categoriaDe.count(nombre) ? categoriaDe[nombre] : "";
            if (descs != nxlinks.end() && descs->is_object() && descs->contains(nombre) && (*descs)[nombre].is_string()) a.desc = (*descs)[nombre].get<std::string>();
            this->apps.push_back(a);
        }
    }
    for (const char* clave : SOLO_EN_SU_SECCION) {
        if (conPestanaPorts && std::string(clave) == "apps_ports") continue;
        const auto sec = nxlinks.find(clave);
        if (sec == nxlinks.end() || !sec->is_object()) continue;
        for (const auto& [nombre, url] : sec->items()) {
            if (!url.is_string()) continue;
            App a;
            a.nombre = nombre;
            a.url = url.get<std::string>();
            a.categoria = categoriaDe.count(nombre) ? categoriaDe[nombre] : "";
            a.soloEn = clave;
            this->apps.push_back(a);
        }
    }
    /* Las apps del pack que hay en la SD: solo en su chip, nunca en «Todas»
       (ahí ya está cada app del catálogo con su propia versión). */
    this->delPack = appsPack::delPackActual(this->manifiestoPack);
    for (int i = 0; i < (int)this->delPack.size(); ++i) {
        const auto& p = this->delPack[i];
        App a;
        a.nombre = p.nombre;
        a.url = p.catalogo;  // para el ícono, si la app está en el catálogo
        a.categoria = p.tipo;
        a.soloEn = "_pack";
        a.pack = i;
        this->apps.push_back(a);
    }
    this->releer(false);

    this->registerAction("menus/apps_pack/todas_accion"_i18n, brls::Key::Y, [this] {
        if (!this->chips.empty() && this->chips[this->chip].clave == "_pack") {
            const auto lista = this->packPendientes();
            if (!lista.empty()) this->instalarDelPack(lista);
        }
        return true;
    }, true);

    this->registerAction("", brls::Key::DUP, [this] {
        this->elegida = std::max(0, this->elegida - 1);
        return true;
    }, true);
    this->registerAction("", brls::Key::DDOWN, [this] {
        this->elegida = std::min((int)this->visibles().size() - 1, this->elegida + 1);
        if (this->elegida < 0) this->elegida = 0;
        return true;
    }, true);
    this->registerAction("", brls::Key::DLEFT, [] { return false; }, true);  // vuelve al menu
    this->registerAction("", brls::Key::DRIGHT, [] { return true; }, true);
    this->registerAction("", brls::Key::L, [this] {
        this->chip = (this->chip + this->chips.size() - 1) % std::max<size_t>(1, this->chips.size());
        this->elegida = this->primera = 0;
        return true;
    }, true);
    this->registerAction("menus/inicio/apps_categoria"_i18n, brls::Key::R, [this] {
        this->chip = (this->chip + 1) % std::max<size_t>(1, this->chips.size());
        this->elegida = this->primera = 0;
        return true;
    });
    this->registerAction("menus/inicio/apps_buscar"_i18n, brls::Key::X, [this] {
        ListDownloadTab::openSearch(contentType::apps, this->nxlinks);
        return true;
    });
    this->registerAction("menus/inicio/apps_instalar"_i18n, brls::Key::A, [this] {
        this->instalar();
        return true;
    });
}

AppsPage::~AppsPage()
{
    NVGcontext* vg = brls::Application::getNVGContext();
    for (auto& a : this->apps)
        if (a.imagen > 0) nvgDeleteImage(vg, a.imagen);
    delete this->oculto;
    for (auto& [clave, lista] : this->ocultosSeccion)
        delete lista;
}

// Estado de cada app y los chips (el de "Con actualizacion" solo si hay alguna).
void AppsPage::releer(bool conPack)
{
    this->genInstalaciones = appUpdates::generacionInstalaciones();
    const nlohmann::ordered_json instaladas = appUpdates::installed();
    int nuevas = 0;
    int pendientes = 0;
    for (auto& a : this->apps) {
        if (a.pack >= 0) {
            appsPack::App& p = this->delPack[a.pack];
            if (conPack) appsPack::releerEstado(p);
            a.estado = p.estado == appsPack::AlDia ? 1 : (p.estado == appsPack::SinInstalar ? 0 : 2);
            a.tengo = p.tengo;
            a.nueva = p.version;
            if (a.estado == 2) ++pendientes;
            continue;
        }
        a.estado = appUpdates::hasUpdate(this->nxlinks, instaladas, a.url) ? 2 : (appUpdates::isInstalled(this->nxlinks, a.url) ? 1 : 0);
        a.tengo = appUpdates::installedVersion(a.url);
        a.nueva = appUpdates::publishedVersion(this->nxlinks, a.url);
        if (a.estado == 2) ++nuevas;
    }
    const std::string antes = this->chips.empty() ? "" : this->chips[this->chip].clave;
    this->chips.clear();
    if (nuevas) this->chips.push_back({fmt::format("menus/inicio/apps_chip_nuevas"_i18n, nuevas), "_nuevas"});
    if (!this->delPack.empty())
        this->chips.push_back({pendientes ? fmt::format("menus/apps_pack/chip"_i18n, pendientes) : "menus/apps_pack/chip_sin"_i18n, "_pack"});
    this->chips.push_back({"menus/inicio/apps_chip_todas"_i18n, ""});
    for (const auto& s : SECCIONES)
        if (this->porSeccion.count(s.clave)) this->chips.push_back({i18n::getStr(s.etiqueta), s.clave});
    this->chip = 0;
    for (int i = 0; i < (int)this->chips.size(); ++i)
        if (this->chips[i].clave == antes) this->chip = i;
}

std::vector<int> AppsPage::visibles() const
{
    std::vector<int> out;
    if (this->chips.empty()) return out;
    const std::string& clave = this->chips[this->chip].clave;
    for (int i = 0; i < (int)this->apps.size(); ++i) {
        const App& a = this->apps[i];
        if (clave == "_pack" ? a.pack >= 0
                             : clave == "_nuevas" ? a.estado == 2 && a.pack < 0
                               : clave.empty() ? a.soloEn.empty()
                               : (a.soloEn.empty() || a.soloEn == clave) && (this->porSeccion.count(clave) &&
                                                   std::find(this->porSeccion.at(clave).begin(), this->porSeccion.at(clave).end(), a.nombre) != this->porSeccion.at(clave).end()))
            out.push_back(i);
    }
    // Primero lo que tiene version nueva, despues lo instalado.
    std::stable_sort(out.begin(), out.end(), [this](int x, int y) { return this->apps[x].estado > this->apps[y].estado; });
    return out;
}

void AppsPage::instalar()
{
    const auto v = this->visibles();
    if (this->elegida < 0 || this->elegida >= (int)v.size()) return;
    if (this->apps[v[this->elegida]].pack >= 0) {
        this->instalarDelPack({this->delPack[this->apps[v[this->elegida]].pack]});
        return;
    }
    const std::string& nombre = this->apps[v[this->elegida]].nombre;
    const std::string& soloEn = this->apps[v[this->elegida]].soloEn;
    ListDownloadTab*& lista = soloEn.empty() ? this->oculto : this->ocultosSeccion[soloEn];
    if (!lista) {
        util::LecturaSwitch lectura;  // /switch se lee una vez para toda la lista
        lista = soloEn.empty() ? new ListDownloadTab(contentType::apps, this->nxlinks) : new ListDownloadTab(contentType::apps, this->nxlinks, soloEn);
    }
    for (size_t i = 0; i < lista->getViewsCount(); ++i)
        if (auto* item = dynamic_cast<brls::ListItem*>(lista->getChild(i)))
            if (item->getLabel() == nombre) {
                item->onClick();
                return;
            }
}

std::vector<appsPack::App> AppsPage::packPendientes() const
{
    std::vector<appsPack::App> out;
    for (const auto& p : this->delPack)
        if (p.estado == appsPack::Nueva || p.estado == appsPack::Incompleta) out.push_back(p);
    return out;
}

void AppsPage::instalarDelPack(const std::vector<appsPack::App>& lista)
{
    std::string filas;
    int mostradas = 0;
    for (const auto& a : lista) {
        if (++mostradas > 10) {
            filas += fmt::format("… +{}\n", lista.size() - 10);
            break;
        }
        std::string v = a.version;
        if (!a.tengo.empty() && !a.version.empty() && a.tengo != a.version) v = a.tengo + " → " + a.version;
        filas += a.nombre + (v.empty() ? "" : "   " + v) + "\n";
    }
    brls::StagedAppletFrame* frame = new brls::StagedAppletFrame();
    frame->setTitle("menus/apps_pack/titulo"_i18n);
    frame->addStage(new ConfirmPage(frame, fmt::format("menus/apps_pack/confirmar"_i18n, filas)));
    auto mensaje = std::make_shared<std::string>();
    const nlohmann::ordered_json manifiesto = this->manifiestoPack;
    frame->addStage(new WorkerPage(frame, "menus/apps_pack/instalando"_i18n, [lista, manifiesto, mensaje]() {
        const appsPack::Resultado r = appsPack::instalar(manifiesto, lista);
        std::string t = r.bien ? fmt::format("menus/apps_pack/listo"_i18n, r.bien) : "";
        if (r.mal) {
            std::string nombres;
            for (const auto& n : r.fallaron) nombres += (nombres.empty() ? "" : ", ") + n;
            t += (t.empty() ? "" : "\n\n") + fmt::format("menus/apps_pack/fallaron"_i18n, nombres);
        }
        if (r.reiniciar) {
            t += "\n\n" + "menus/apps_pack/reiniciar"_i18n;
            led::reinicio();
        }
        *mensaje = t;
    }));
    frame->addStage((new ConfirmPage_Deferred(frame, mensaje))->returnToList());
    brls::Application::pushView(frame);
}

int AppsPage::icono(NVGcontext* vg, App& app)
{
    if (app.imagen >= 0) return app.imagen;
    app.imagen = 0;
    // Del pack sin ícono del catálogo: el de su .nro, si está en la SD.
    if (app.pack >= 0 && (app.url.empty() || appUpdates::rutaIcono(this->nxlinks, app.url).empty())) {
        const std::string& principal = this->delPack[app.pack].principal;
        if (principal.size() > 4 && principal.substr(principal.size() - 4) == ".nro") {
            std::vector<uint8_t> propio = appUpdates::iconoDeNro("/" + principal);
            if (!propio.empty()) app.imagen = nvgCreateImageMem(vg, 0, propio.data(), propio.size());
            if (app.imagen < 0) app.imagen = 0;
            if (app.imagen > 0 || app.url.empty()) return app.imagen;
        }
    }
    if (app.url.empty()) return 0;
    const std::string ruta = appUpdates::rutaIcono(this->nxlinks, app.url);
    if (!ruta.empty())
        app.imagen = nvgCreateImage(vg, ruta.c_str(), 0);
    else {
        std::vector<uint8_t> propio = appUpdates::iconoInstalado(this->nxlinks, app.url);
        if (!propio.empty()) app.imagen = nvgCreateImageMem(vg, 0, propio.data(), propio.size());
    }
    if (app.imagen < 0) app.imagen = 0;
    return app.imagen;
}

void AppsPage::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    if (appUpdates::generacionInstalaciones() != this->genInstalaciones) this->releer();
    if (!this->fuenteIntentada) {
        this->fuenteIntentada = true;
        this->fuente = nvgFindFont(vg, "upheaval");
        if (this->fuente < 0) this->fuente = nvgCreateFont(vg, "upheaval", "romfs:/fonts/upheavtt.ttf");
    }
    const int regular = ctx->fontStash->regular;
    const NVGcolor texto = this->a(ctx->theme->textColor);
    const NVGcolor tenue = this->a(ctx->theme->descriptionColor);
    const NVGcolor fondo = this->a(oqb::velo(oqb::oscuro() ? 12 : 10));
    const NVGcolor borde = this->a(ctx->theme->sidebarSeparatorColor);
    const NVGcolor foco = this->a(ctx->theme->highlightColor1);
    const bool conFoco = this->isFocused();

    const float panel = 340.0f;
    const float lx = x + 36.0f, lw = width - panel - 36.0f - 24.0f;
    float ly = y + 30.0f;

    // Titulo y buscador.
    nvgFontFaceId(vg, this->fuente >= 0 ? this->fuente : regular);
    nvgFontSize(vg, 40.0f);
    nvgTextLetterSpacing(vg, 1.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, texto);
    nvgBeginPath(vg);
    nvgText(vg, lx, ly + 22.0f, "menus/inicio/titulo_apps"_i18n.c_str(), nullptr);
    const float tituloW = nvgTextBounds(vg, 0, 0, "menus/inicio/titulo_apps"_i18n.c_str(), nullptr, nullptr);
    nvgTextLetterSpacing(vg, 0.0f);
    nvgFontFaceId(vg, regular);
    {
        const float bx = lx + tituloW + 20.0f, bw = lx + lw - bx;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, bx, ly, bw, 44.0f, 12.0f);
        nvgFillColor(vg, fondo);
        nvgFill(vg);
        nvgStrokeColor(vg, borde);
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);
        nvgFontSize(vg, 16.0f);
        nvgFillColor(vg, tenue);
        nvgBeginPath(vg);
        nvgText(vg, bx + 16.0f, ly + 22.0f, fmt::format("menus/inicio/apps_buscar_en"_i18n, this->apps.size() - this->delPack.size()).c_str(), nullptr);
    }
    ly += 60.0f;

    // Chips de categoria; el elegido relleno.
    {
        float cx = lx;
        nvgFontSize(vg, 14.0f);
        for (int i = 0; i < (int)this->chips.size(); ++i) {
            const std::string& t = this->chips[i].texto;
            const float w = nvgTextBounds(vg, 0, 0, t.c_str(), nullptr, nullptr) + 26.0f;
            // Todas las categorias a la vista: la que no entra pasa a la fila de abajo.
            if (cx + w > lx + lw && cx > lx) {
                cx = lx;
                ly += 40.0f;
            }
            const bool sel = i == this->chip;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx, ly, w, 32.0f, 16.0f);
            if (sel) {
                nvgFillColor(vg, texto);
                nvgFill(vg);
            }
            else {
                nvgStrokeColor(vg, borde);
                nvgStrokeWidth(vg, 1.0f);
                nvgStroke(vg);
            }
            nvgFillColor(vg, sel ? this->a(ctx->theme->backgroundColorRGB) : tenue);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgBeginPath(vg);
            nvgText(vg, cx + w / 2, ly + 17.0f, t.c_str(), nullptr);
            cx += w + 8.0f;
        }
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        ly += 46.0f;
    }

    // La lista.
    const auto v = this->visibles();
    if (this->elegida >= (int)v.size()) this->elegida = std::max(0, (int)v.size() - 1);
    const float fila = 64.0f;
    const int caben = std::max(1, (int)((y + height - 10.0f - ly) / fila));
    if (this->elegida < this->primera) this->primera = this->elegida;
    if (this->elegida >= this->primera + caben) this->primera = this->elegida - caben + 1;
    nvgSave(vg);
    nvgScissor(vg, x, ly, width - panel, y + height - ly);
    for (int k = 0; k < caben && this->primera + k < (int)v.size(); ++k) {
        const int idx = this->primera + k;
        App& a = this->apps[v[idx]];
        const float fy = ly + k * fila;
        const bool sel = idx == this->elegida;
        if (sel) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, lx, fy + 2, lw, fila - 4, 12.0f);
            nvgFillColor(vg, fondo);
            nvgFill(vg);
            if (conFoco) {
                nvgStrokeColor(vg, foco);
                nvgStrokeWidth(vg, 2.5f);
                nvgStroke(vg);
            }
        }
        const float lado = 44.0f, ix = lx + 12.0f, iy = fy + (fila - lado) / 2;
        const int img = this->icono(vg, a);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, ix, iy, lado, lado, 10.0f);
        if (img > 0)
            nvgFillPaint(vg, nvgImagePattern(vg, ix, iy, lado, lado, 0, img, this->getAlpha()));
        else
            nvgFillColor(vg, this->a(oqb::velo(30)));
        nvgFill(vg);
        if (img <= 0) {
            nvgFontSize(vg, 14.0f);
            nvgFillColor(vg, texto);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgBeginPath(vg);
            nvgText(vg, ix + lado / 2, iy + lado / 2, iniciales(a.nombre).c_str(), nullptr);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        }
        // Estado a la derecha.
        const bool incompleta = a.pack >= 0 && this->delPack[a.pack].estado == appsPack::Incompleta;
        const std::string est = incompleta ? "menus/apps_pack/incompleta"_i18n : a.estado == 2 ? "menus/apps/new_version"_i18n : (a.estado == 1 ? "menus/apps/installed"_i18n : "menus/inicio/apps_sin_instalar"_i18n);
        nvgFontSize(vg, 13.0f);
        const float ew = nvgTextBounds(vg, 0, 0, est.c_str(), nullptr, nullptr) + 20.0f;
        const float ex = lx + lw - 14.0f - ew;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, ex, fy + fila / 2 - 13, ew, 26.0f, 13.0f);
        nvgFillColor(vg, this->a(a.estado == 2 ? nvgRGBA(230, 185, 92, oqb::oscuro() ? 46 : 70) : (a.estado == 1 ? nvgRGBA(52, 213, 131, 46) : oqb::velo(16))));
        nvgFill(vg);
        nvgFillColor(vg, this->a(a.estado == 2 ? oqb::doradoTexto() : (a.estado == 1 ? oqb::verdeTexto() : oqb::grisTexto())));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgBeginPath(vg);
        nvgText(vg, ex + ew / 2, fy + fila / 2 + 1, est.c_str(), nullptr);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        const float tx = ix + lado + 14.0f, tw = ex - 12.0f - tx;
        nvgFontSize(vg, 18.0f);
        nvgFillColor(vg, texto);
        nvgBeginPath(vg);
        nvgText(vg, tx, fy + 22.0f, cortar(vg, a.nombre, tw).c_str(), nullptr);
        nvgFontSize(vg, 13.0f);
        nvgFillColor(vg, tenue);
        nvgBeginPath(vg);
        nvgText(vg, tx, fy + 44.0f, cortar(vg, a.categoria, tw).c_str(), nullptr);
    }
    if (v.empty()) {
        nvgFontSize(vg, 17.0f);
        nvgFillColor(vg, tenue);
        nvgBeginPath(vg);
        nvgText(vg, lx + 10.0f, ly + 24.0f, "menus/inicio/apps_vacio"_i18n.c_str(), nullptr);
    }
    nvgRestore(vg);

    // La ficha de la elegida.
    const float fx = x + width - panel, fw = panel;
    nvgBeginPath(vg);
    nvgRect(vg, fx, y, fw, height);
    nvgFillColor(vg, this->a(oqb::velo(oqb::oscuro() ? 8 : 6)));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, fx, y, 1, height);
    nvgFillColor(vg, borde);
    nvgFill(vg);
    if (this->elegida < (int)v.size()) {
        App& a = this->apps[v[this->elegida]];
        const float px = fx + 28.0f, pw = fw - 56.0f;
        float py = y + 30.0f;
        const int img = this->icono(vg, a);
        const float lado = 104.0f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, px, py, lado, lado, 22.0f);
        if (img > 0)
            nvgFillPaint(vg, nvgImagePattern(vg, px, py, lado, lado, 0, img, this->getAlpha()));
        else
            nvgFillColor(vg, this->a(oqb::velo(30)));
        nvgFill(vg);
        py += lado + 18.0f;
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgFontSize(vg, 23.0f);
        nvgFillColor(vg, texto);
        nvgBeginPath(vg);
        nvgText(vg, px, py, cortar(vg, a.nombre, pw).c_str(), nullptr);
        py += 32.0f;
        nvgFontSize(vg, 15.0f);
        nvgFillColor(vg, tenue);
        nvgBeginPath(vg);
        nvgText(vg, px, py, cortar(vg, a.categoria, pw).c_str(), nullptr);
        py += 30.0f;
        const appsPack::App* p = a.pack >= 0 ? &this->delPack[a.pack] : nullptr;
        const std::string desc = p ? "menus/apps_pack/desc"_i18n + (p->reinicio ? " " + "menus/apps_pack/desc_reinicio"_i18n : "") : a.desc;
        if (!desc.empty()) {
            nvgFontSize(vg, 16.0f);
            nvgTextLineHeight(vg, 1.35f);
            nvgFillColor(vg, this->a(ctx->theme->textColor));
            float caja[4];
            nvgTextBoxBounds(vg, px, py, pw, desc.c_str(), nullptr, caja);
            nvgBeginPath(vg);
            nvgTextBox(vg, px, py, pw, desc.c_str(), nullptr);
            py = caja[3] + 16.0f;
        }
        auto dato = [&](const std::string& etiqueta, const std::string& valor, NVGcolor color) {
            if (valor.empty()) return;
            nvgFontSize(vg, 15.0f);
            nvgFillColor(vg, tenue);
            nvgBeginPath(vg);
            nvgText(vg, px, py, etiqueta.c_str(), nullptr);
            nvgFillColor(vg, color);
            nvgBeginPath(vg);
            nvgText(vg, px + 90.0f, py, cortar(vg, valor, pw - 90.0f).c_str(), nullptr);
            py += 26.0f;
        };
        dato("menus/inicio/apps_tienes"_i18n, a.tengo, texto);
        if (p) {
            dato("menus/apps_pack/en_pack"_i18n, p->version, this->a(a.estado == 2 ? oqb::verdeTexto() : ctx->theme->textColor));
            dato("menus/apps_pack/descarga"_i18n, fmt::format("{:.1f} MB", p->bytes / 1e6), texto);
        }
        else if (a.estado == 2)
            dato("menus/inicio/apps_nueva"_i18n, a.nueva, this->a(oqb::verdeTexto()));
        // El boton de la accion, abajo.
        const float bh = 46.0f, by = y + height - 24.0f - bh;
        // Del pack: «Actualizar las N» con Y, encima del botón de esta app.
        if (p) {
            const auto pend = this->packPendientes();
            if (!pend.empty()) {
                std::uint64_t total = 0;
                for (const auto& x : pend) total += x.bytes;
                const float yb = by - bh - 10.0f;
                nvgBeginPath(vg);
                nvgRoundedRect(vg, px, yb, pw, bh, 12.0f);
                nvgFillColor(vg, this->a(oqb::velo(16)));
                nvgFill(vg);
                nvgFontSize(vg, 17.0f);
                nvgFillColor(vg, texto);
                nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                nvgBeginPath(vg);
                nvgText(vg, px + pw / 2, yb + bh / 2 + 1, fmt::format("menus/apps_pack/todas"_i18n, pend.size(), fmt::format("{:.0f} MB", std::max(1.0, total / 1e6))).c_str(), nullptr);
            }
        }
        nvgBeginPath(vg);
        nvgRoundedRect(vg, px, by, pw, bh, 12.0f);
        nvgFillColor(vg, this->a(oqb::esmeralda()));
        nvgFill(vg);
        nvgFontSize(vg, 18.0f);
        nvgFillColor(vg, this->a(oqb::oscuro() ? nvgRGB(6, 20, 12) : nvgRGB(255, 255, 255)));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgBeginPath(vg);
        const bool falta = p && p->estado == appsPack::Incompleta;
        const std::string accion = falta ? "menus/apps_pack/boton_completar"_i18n : a.estado == 2 ? "menus/inicio/apps_boton_actualizar"_i18n : (a.estado == 1 ? "menus/inicio/apps_boton_reinstalar"_i18n : "menus/inicio/apps_boton_instalar"_i18n);
        nvgText(vg, px + pw / 2, by + bh / 2 + 1, accion.c_str(), nullptr);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    }
}
