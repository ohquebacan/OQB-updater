#include "ports_page.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>

#include "actividad.hpp"
#include "app_updates.hpp"
#include "cola.hpp"
#include "cola_page.hpp"
#include "confirm_page.hpp"
#include "constants.hpp"
#include "extract.hpp"
#include "forwarder.hpp"
#include "fs.hpp"
#include "list_download_tab.hpp"
#include "oqb_theme.hpp"
#include "progress_event.hpp"
#include "utils.hpp"
#include "worker_page.hpp"
#include "zip_vivo.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {

    /* Las partes que empezaron a instalarse y no terminaron: su zip pudo quedar
       a medio descomprimir, asi que no cuentan como instaladas aunque esten sus
       archivos. Se anotan al empezar a bajarlas y se borran al terminar. */
    constexpr const char PENDIENTES[] = "/config/aio-switch-updater/ports_pendientes.json";

    std::set<std::string> leerPendientes()
    {
        std::set<std::string> out;
        std::ifstream f(PENDIENTES);
        if (!f) return out;
        const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
        if (j.is_array())
            for (const auto& u : j)
                if (u.is_string()) out.insert(u.get<std::string>());
        return out;
    }

    void marcarPendiente(const std::string& url, bool pendiente)
    {
        std::set<std::string> s = leerPendientes();
        if (pendiente ? !s.insert(url).second : !s.erase(url)) return;
        nlohmann::json j = nlohmann::json::array();
        for (const auto& u : s) j.push_back(u);
        fs::createTree("/config/aio-switch-updater/");
        std::ofstream f(PENDIENTES, std::ios::trunc);
        f << j.dump();
    }

    long long bytesDe(const nlohmann::ordered_json& nxlinks, const std::string& url)
    {
        const auto all = nxlinks.find(appUpdates::NXLINKS_KEY);
        if (all == nxlinks.end() || !all->is_object()) return 0;
        const auto e = all->find(url);
        if (e == all->end() || !e->is_object()) return 0;
        const auto b = e->find("bytes");
        return b != e->end() && b->is_number_integer() ? b->get<long long>() : 0;
    }

    // En la misma unidad que «GB Disponibles» de la barra de abajo (GiB).
    // La entrada de app-versions.json de un zip (o nullptr).
    const nlohmann::ordered_json* versionDe(const nlohmann::ordered_json& nxlinks, const std::string& url)
    {
        const auto all = nxlinks.find(appUpdates::NXLINKS_KEY);
        if (all == nxlinks.end() || !all->is_object()) return nullptr;
        const auto e = all->find(url);
        return e != all->end() && e->is_object() ? &*e : nullptr;
    }

    // El primer .nro que el catalogo conoce de esta parte (aunque no este en la SD).
    std::string nroDe(const nlohmann::ordered_json& nxlinks, const std::string& url)
    {
        const auto* e = versionDe(nxlinks, url);
        if (!e) return "";
        const auto m = e->find("marcas");
        if (m == e->end() || !m->is_array()) return "";
        // La que se llama como su carpeta (t6game.nro en /switch/t6game/); si ninguna, la primera.
        std::string primera;
        for (const auto& r : *m) {
            if (!r.is_string()) continue;
            const std::string ruta = r.get<std::string>();
            std::string bajo = ruta;
            for (auto& c : bajo) c = std::tolower(static_cast<unsigned char>(c));
            if (bajo.size() <= 4 || bajo.compare(bajo.size() - 4, 4, ".nro") != 0) continue;
            if (primera.empty()) primera = ruta;
            const size_t barra = bajo.rfind('/');
            const size_t antes = barra == std::string::npos || barra == 0 ? std::string::npos : bajo.rfind('/', barra - 1);
            if (antes != std::string::npos && bajo.substr(antes + 1, barra - antes - 1) == bajo.substr(barra + 1, bajo.size() - barra - 5)) return ruta;
        }
        return primera;
    }

    double gb(long long bytes) { return bytes / 1073741824.0; }

    // Menos de 1 GB en MB: si no, un port chico decia «0.0 GB».
    std::string tamano(long long bytes)
    {
        if (bytes <= 0) return "";
        if (bytes < 1073741824LL) return fmt::format("{} MB", std::max<long long>(1, (bytes + 524288) / 1048576));
        return fmt::format("{:.1f} GB", gb(bytes));
    }

    std::string minusculas(std::string s)
    {
        for (auto& c : s) c = std::tolower(static_cast<unsigned char>(c));
        return s;
    }

    std::string cortar(NVGcontext* vg, const std::string& texto, float ancho)
    {
        if (ancho <= 0) return "";
        if (nvgTextBounds(vg, 0, 0, texto.c_str(), nullptr, nullptr) <= ancho) return texto;
        std::string s = texto;
        while (!s.empty()) {
            do s.pop_back();
            while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80);
            const std::string prueba = s + "…";
            if (nvgTextBounds(vg, 0, 0, prueba.c_str(), nullptr, nullptr) <= ancho) return prueba;
        }
        return "…";
    }

    void check(NVGcontext* vg, float cx, float cy, float r, NVGcolor fondo, NVGcolor trazo)
    {
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, r);
        nvgFillColor(vg, fondo);
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - r * 0.45f, cy + r * 0.02f);
        nvgLineTo(vg, cx - r * 0.1f, cy + r * 0.36f);
        nvgLineTo(vg, cx + r * 0.48f, cy - r * 0.32f);
        nvgStrokeColor(vg, trazo);
        nvgStrokeWidth(vg, r * 0.28f);
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);
        nvgStroke(vg);
    }

    // Una pildora de texto; devuelve su ancho.
    float pildora(NVGcontext* vg, float x, float cy, const std::string& t, float tam, NVGcolor fondo, NVGcolor color, float alto = 0)
    {
        nvgFontSize(vg, tam);
        const float w = nvgTextBounds(vg, 0, 0, t.c_str(), nullptr, nullptr) + tam * 1.1f;
        const float h = alto > 0 ? alto : tam * 1.75f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, cy - h / 2, w, h, h / 2);
        nvgFillColor(vg, fondo);
        nvgFill(vg);
        nvgFillColor(vg, color);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgBeginPath(vg);
        nvgText(vg, x + w / 2, cy + 1, t.c_str(), nullptr);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        return w;
    }

    int crearPortada(NVGcontext* vg, const nlohmann::ordered_json& nxlinks, const ports::Port& p)
    {
        int img = 0;
        for (const auto& parte : p.partes) {
            const std::string ruta = appUpdates::rutaIcono(nxlinks, parte.url);
            if (!ruta.empty()) {
                img = nvgCreateImage(vg, ruta.c_str(), 0);
                if (img > 0) break;
            }
        }
        return img > 0 ? img : 0;
    }

    std::vector<ports::Port> leerPorts(const nlohmann::ordered_json& nxlinks)
    {
        std::vector<ports::Port> out;
        const auto sec = nxlinks.find("ports");
        if (sec == nxlinks.end() || !sec->is_object()) return out;
        const std::set<std::string> pendientes = leerPendientes();
        util::LecturaSwitch lectura;  // /switch se lee una vez para todas las marcas
        const nlohmann::ordered_json registro = appUpdates::installed();
        for (const auto& [nombre, datos] : sec->items()) {
            if (!datos.is_object()) continue;
            ports::Port p;
            p.nombre = nombre;
            const auto act = datos.find("actualizacion");
            p.actualizacion = act != datos.end() && act->is_boolean() && act->get<bool>();
            const auto partes = datos.find("partes");
            if (partes == datos.end() || !partes->is_array()) continue;
            /* Con "secciones" (4.8.0, ports propios): cada una con sus partes,
               y en la ficha se marca que instalar. Si no, "partes" de siempre. */
            std::vector<std::pair<std::string, int>> urls;
            const auto secs = datos.find("secciones");
            if (secs != datos.end() && secs->is_array()) {
                for (const auto& sj : *secs) {
                    if (!sj.is_object() || !sj.contains("partes") || !sj["partes"].is_array()) continue;
                    ports::Seccion sec;
                    sec.nombre = sj.value("nombre", "");
                    sec.grupo = sj.contains("grupo") && sj["grupo"].is_string() ? sj["grupo"].get<std::string>() : "";
                    sec.opcional = sec.grupo.empty() && sj.value("opcional", false);
                    sec.marcada = sj.value("marcada", true);
                    const int idx = p.secciones.size();
                    p.secciones.push_back(sec);
                    for (const auto& u : sj["partes"])
                        if (u.is_string()) urls.emplace_back(u.get<std::string>(), idx);
                }
            }
            /* Accesos directos propios (4.8.1): Black Ops II tiene multijugador y
               zombis, cada uno con su app y su icono. */
            const auto acc = datos.find("accesos");
            if (acc != datos.end() && acc->is_array()) {
                for (const auto& a : *acc) {
                    if (!a.is_object() || !a.contains("nro") || !a["nro"].is_string()) continue;
                    const std::string nro = a["nro"].get<std::string>();
                    p.accesos.push_back(nro);
                    if (a.contains("icono") && a["icono"].is_string()) fwd::agregarIconoPropio(nro.substr(nro.rfind('/') + 1), a["icono"].get<std::string>());
                }
            }
            if (urls.empty()) {
                p.secciones.clear();
                for (const auto& u : *partes)
                    if (u.is_string()) urls.emplace_back(u.get<std::string>(), 0);
            }
            for (const auto& [url, sec] : urls) {
                ports::Parte parte;
                parte.url = url;
                parte.seccion = sec;
                parte.bytes = bytesDe(nxlinks, parte.url);
                if (const auto* e = versionDe(nxlinks, parte.url)) {
                    const auto d = e->find("descomprimido");
                    if (d != e->end() && d->is_number_integer()) parte.descomprimido = d->get<long long>();
                }
                parte.instalada = !pendientes.count(parte.url) && appUpdates::isInstalled(nxlinks, parte.url);
                /* Con secciones, las partes comparten archivos (la actualizacion
                   reemplaza el .nro del juego; el espanol y el ingles son los
                   mismos archivos): cuentan solo si esta app las instalo. */
                if (!p.secciones.empty() && !(registro.is_object() && registro.contains(parte.url))) parte.instalada = false;
                if (const auto* e = versionDe(nxlinks, parte.url)) {
                    const auto f = e->find("fecha");
                    if (f != e->end() && f->is_string() && f->get<std::string>() > p.fecha) p.fecha = f->get<std::string>();
                }
                /* El .nro y, si el catalogo lo trae, el icono del acceso directo
                   (la portada del port en JPEG de 256, el que acepta el HOME). */
                const std::string nro = nroDe(nxlinks, parte.url);
                if (!nro.empty()) {
                    if (p.nro.empty() && util::existeEnSd(nro)) p.nro = nro;
                    if (const auto* e = versionDe(nxlinks, parte.url)) {
                        const auto ic = e->find("icono_acceso");
                        if (ic != e->end() && ic->is_string()) fwd::agregarIconoPropio(nro.substr(nro.rfind('/') + 1), ic->get<std::string>());
                    }
                }
                p.partes.push_back(parte);
            }
            if (!p.partes.empty()) out.push_back(p);
        }
        return out;
    }

    /* La ficha: portada, partes con su tamano y estado, el espacio y el boton.
       Es la primera etapa; A pasa a instalar (si hay espacio). */
    class PortDetalle : public brls::View
    {
    public:
        PortDetalle(brls::StagedAppletFrame* frame, const nlohmann::ordered_json& nxlinks, const ports::Port& port, std::vector<int> aInstalar, long long necesita, long long libre)
            : frame(frame), nxlinks(nxlinks), port(port), aInstalar(std::move(aInstalar)), necesita(necesita), libre(libre)
        {
            /* Con secciones: una casilla por seccion. Arriba/abajo se mueve, A
               marca (o elige, en un grupo) y en el boton instala lo marcado. Lo
               obligatorio va siempre; de cada grupo, la instalada o la primera. */
            if (this->port.conSecciones()) {
                const int n = this->port.secciones.size();
                this->marcada.assign(n, false);
                std::set<std::string> grupoElegido;
                for (int sec = 0; sec < n; ++sec) {
                    const auto& s = this->port.secciones[sec];
                    const bool completa = this->port.partesDe(sec) > 0 && this->port.instaladasDe(sec) == this->port.partesDe(sec);
                    if (!s.grupo.empty()) {
                        if (completa && !grupoElegido.count(s.grupo)) {
                            for (int k = 0; k < n; ++k)
                                if (this->port.secciones[k].grupo == s.grupo) this->marcada[k] = false;
                            this->marcada[sec] = true;
                            grupoElegido.insert(s.grupo);
                        }
                    }
                    else
                        this->marcada[sec] = s.opcional ? s.marcada : true;
                }
                // De cada grupo sin ninguna instalada, la primera.
                for (int sec = 0; sec < n; ++sec) {
                    const auto& g = this->port.secciones[sec].grupo;
                    if (g.empty() || grupoElegido.count(g)) continue;
                    this->marcada[sec] = true;
                    grupoElegido.insert(g);
                }
                this->cursor = n;  // empieza en el boton
                this->registerAction("", brls::Key::DUP, [this] {
                    if (this->cursor > 0) --this->cursor;
                    this->actualizarHint();
                    return true;
                }, true);
                this->registerAction("", brls::Key::DDOWN, [this] {
                    if (this->cursor < (int)this->port.secciones.size()) ++this->cursor;
                    this->actualizarHint();
                    return true;
                }, true);
            }
            const std::string boton = this->port.conSecciones() ? this->textoBoton() : this->textoBoton();
            this->registerAction(boton, brls::Key::A, [this] {
                if (this->port.conSecciones()) {
                    if (this->cursor < (int)this->port.secciones.size()) {
                        this->tocar(this->cursor);
                        return true;
                    }
                    const std::vector<int> partes = this->seleccion();
                    if (partes.empty()) return true;
                    const long long nec = this->necesitaSeleccion();
                    if (nec > 0 && this->libre >= 0 && nec > this->libre) {
                        brls::Dialog* d = new brls::Dialog(fmt::format("menus/ports/sin_espacio"_i18n, tamano(nec), tamano(this->libre)));
                        d->addButton("menus/common/ok"_i18n, [d](brls::View*) { d->close(); });
                        d->open();
                        return true;
                    }
                    const nlohmann::ordered_json nx = this->nxlinks;
                    ports::Port p = this->port;
                    p.imagen = -1;
                    brls::Application::popView(brls::ViewAnimation::FADE, [nx, p, partes] { ports::instalar(nx, p, partes); });
                    return true;
                }
                if (this->necesita > 0 && this->libre >= 0 && this->necesita > this->libre) {
                    brls::Dialog* d = new brls::Dialog(fmt::format("menus/ports/sin_espacio"_i18n, tamano(this->necesita), tamano(this->libre)));
                    d->addButton("menus/common/ok"_i18n, [d](brls::View*) { d->close(); });
                    d->open();
                    return true;
                }
                {
                    const nlohmann::ordered_json nx = this->nxlinks;
                    ports::Port p = this->port;
                    p.imagen = -1;
                    const std::vector<int> partes = this->aInstalar;
                    brls::Application::popView(brls::ViewAnimation::FADE, [nx, p, partes] { ports::instalar(nx, p, partes); });
                }
                return true;
            });
            // Instalado (aunque sea a medias): X lo elimina, con confirmacion.
            bool algo = false;
            for (const auto& pt : this->port.partes) algo = algo || pt.instalada;
            if (algo) {
                this->registerAction("menus/ports/eliminar"_i18n, brls::Key::X, [this] {
                    const nlohmann::ordered_json nx = this->nxlinks;
                    ports::Port p = this->port;
                    p.imagen = -1;
                    brls::Dialog* d = new brls::Dialog(fmt::format("menus/ports/eliminar_pregunta"_i18n, p.nombre));
                    d->addButton("menus/common/cancel"_i18n, [d](brls::View*) { d->close(); });
                    d->addButton("menus/ports/eliminar"_i18n, [d, nx, p](brls::View*) {
                        d->close([nx, p] { brls::Application::popView(brls::ViewAnimation::FADE, [nx, p] { ports::eliminar(nx, p); }); });
                    });
                    d->open();
                    return true;
                });
            }
            // Instalado: Y vuelve a crear el acceso directo del menu HOME.
            if (this->port.estado() == 1 && (!this->port.nro.empty() || !this->port.accesos.empty())) {
                const std::vector<std::string> nros = this->port.accesos.empty() ? std::vector<std::string>{this->port.nro} : this->port.accesos;
                const std::string nombre = this->port.nombre;
                this->registerAction("menus/ports/acceso_directo"_i18n, brls::Key::Y, [nros, nombre] {
                    brls::StagedAppletFrame* frame = new brls::StagedAppletFrame();
                    frame->setTitle(nombre);
                    ListDownloadTab::addForwarderStages(frame, nros);
                    brls::Application::pushView(frame);
                    return true;
                });
            }
        }
        ~PortDetalle() override
        {
            if (this->port.imagen > 0) nvgDeleteImage(brls::Application::getNVGContext(), this->port.imagen);
        }
        brls::View* getDefaultFocus() override { return this; }
        void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override {}
        // Sin el marco de foco alrededor de toda la ficha.
        void onFocusGained() override { this->focused = true; }
        void onFocusLost() override { this->focused = false; }

        void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override
        {
            const NVGcolor texto = this->a(ctx->theme->textColor);
            const NVGcolor tenue = this->a(ctx->theme->descriptionColor);
            const NVGcolor fondo = this->a(oqb::velo(oqb::oscuro() ? 12 : 10));
            nvgFontFaceId(vg, ctx->fontStash->regular);
            if (this->port.imagen < 0) this->port.imagen = crearPortada(vg, this->nxlinks, this->port);

            const float m = 48.0f, lado = 200.0f;
            const float px = x + m, py = y + 30.0f;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, px, py, lado, lado, 20.0f);
            if (this->port.imagen > 0)
                nvgFillPaint(vg, nvgImagePattern(vg, px, py, lado, lado, 0, this->port.imagen, this->getAlpha()));
            else
                nvgFillColor(vg, this->a(oqb::velo(30)));
            nvgFill(vg);

            // Debajo de la portada: tamano y estado.
            float dy = py + lado + 26.0f;
            const int est = this->port.estado();
            const std::string meta = this->port.partesDe(0) > 1 && this->port.totalDe(0) > 0
                                         ? fmt::format("menus/ports/meta_partes"_i18n, tamano(this->port.totalDe(0)), this->port.partesDe(0))
                                         : tamano(this->port.totalDe(0));
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            if (!meta.empty()) {
                nvgFontSize(vg, 16.0f);
                nvgFillColor(vg, texto);
                nvgBeginPath(vg);
                nvgText(vg, px, dy, meta.c_str(), nullptr);
                dy += 28.0f;
            }
            nvgFontSize(vg, 15.0f);
            nvgFillColor(vg, est == 1 ? this->a(oqb::verdeTexto()) : est == 2 ? this->a(oqb::doradoTexto()) : tenue);
            nvgBeginPath(vg);
            nvgText(vg, px, dy, (est == 1 ? "menus/ports/estado_si"_i18n : est == 2 ? "menus/ports/estado_medias"_i18n : "menus/ports/estado_no"_i18n).c_str(), nullptr);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

            // A la derecha: nombre, explicacion, partes, espacio y boton.
            const float rx = px + lado + 40.0f, rw = x + width - m - rx;
            float ry = py;
            nvgFontSize(vg, 30.0f);
            nvgTextLineHeight(vg, 1.1f);
            nvgFillColor(vg, texto);
            float caja[4];
            nvgTextBoxBounds(vg, rx, ry, rw, this->port.nombre.c_str(), nullptr, caja);
            nvgBeginPath(vg);
            nvgTextBox(vg, rx, ry, rw, this->port.nombre.c_str(), nullptr);
            ry = caja[3] + 14.0f;

            if (this->port.partesDe(0) > 1 || this->port.actualizacion) {
                const std::string t = this->port.actualizacion ? "menus/ports/actualizacion"_i18n : fmt::format("menus/ports/partes"_i18n, this->port.partesDe(0));
                pildora(vg, rx, ry + 12.0f, t, 13.0f, this->a(this->port.actualizacion ? nvgRGBA(143, 211, 240, 40) : nvgRGBA(242, 209, 107, 36)),
                        this->a(this->port.actualizacion ? nvgRGB(143, 211, 240) : oqb::doradoTexto()));
                ry += 38.0f;
            }

            const std::string explica = this->port.conSecciones() ? "menus/ports/explica_secciones"_i18n
                                        : this->port.actualizacion ? "menus/ports/explica_actualizacion"_i18n
                                        : est == 2               ? "menus/ports/explica_medias"_i18n
                                        : this->port.partes.size() > 1 ? fmt::format("menus/ports/explica_partes"_i18n, this->port.partes.size())
                                                                       : "menus/ports/explica_una"_i18n;
            nvgFontSize(vg, 16.0f);
            nvgTextLineHeight(vg, 1.4f);
            nvgFillColor(vg, this->a(oqb::grisTexto()));
            nvgTextBoxBounds(vg, rx, ry, rw, explica.c_str(), nullptr, caja);
            nvgBeginPath(vg);
            nvgTextBox(vg, rx, ry, rw, explica.c_str(), nullptr);
            ry = caja[3] + 16.0f;

            // Con secciones: una fila por seccion, con su casilla.
            if (this->port.conSecciones()) {
                const float fh = 52.0f;
                for (int sec = 0; sec < (int)this->port.secciones.size(); ++sec) {
                    const ports::Seccion& sc = this->port.secciones[sec];
                    const int n = this->port.partesDe(sec);
                    if (n == 0) continue;
                    const int si = this->port.instaladasDe(sec);
                    const bool va = this->marcada[sec];
                    const bool fija = sc.grupo.empty() && !sc.opcional;
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, rx, ry, rw, fh - 6.0f, 11.0f);
                    nvgFillColor(vg, fondo);
                    nvgFill(vg);
                    if (this->cursor == sec) {
                        nvgBeginPath(vg);
                        nvgRoundedRect(vg, rx + 1.0f, ry + 1.0f, rw - 2.0f, fh - 8.0f, 10.0f);
                        nvgStrokeColor(vg, this->a(oqb::esmeralda()));
                        nvgStrokeWidth(vg, 2.5f);
                        nvgStroke(vg);
                    }
                    const float cy = ry + (fh - 6.0f) / 2, cx = rx + 26.0f;
                    // Casilla (opcional u obligatoria) o circulo (una de un grupo).
                    nvgBeginPath(vg);
                    if (sc.grupo.empty())
                        nvgRoundedRect(vg, cx - 11.0f, cy - 11.0f, 22.0f, 22.0f, 6.0f);
                    else
                        nvgCircle(vg, cx, cy, 11.5f);
                    if (va) {
                        nvgFillColor(vg, this->a(fija ? oqb::velo(40) : oqb::esmeralda()));
                        nvgFill(vg);
                        if (sc.grupo.empty())
                            check(vg, cx, cy, 11.0f, this->a(nvgRGBA(0, 0, 0, 0)), this->a(fija ? texto : nvgRGB(6, 20, 12)));
                        else {
                            nvgBeginPath(vg);
                            nvgCircle(vg, cx, cy, 4.5f);
                            nvgFillColor(vg, this->a(nvgRGB(6, 20, 12)));
                            nvgFill(vg);
                        }
                    }
                    else {
                        nvgStrokeColor(vg, this->a(oqb::velo(60)));
                        nvgStrokeWidth(vg, 2.0f);
                        nvgStroke(vg);
                    }
                    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                    nvgFontSize(vg, 17.0f);
                    nvgFillColor(vg, va ? texto : tenue);
                    nvgBeginPath(vg);
                    std::string izq = sc.grupo.empty() ? sc.nombre : sc.grupo + ": " + sc.nombre;
                    if (n > 1) izq = fmt::format("menus/ports/sec_partes"_i18n, izq, n);
                    nvgText(vg, rx + 52.0f, cy, izq.c_str(), nullptr);
                    const long long t = this->port.totalDe(sec);
                    const std::string est = si == n ? "menus/ports/instalada"_i18n
                                            : si > 0 ? fmt::format("menus/ports/sec_de"_i18n, si, n)
                                            : va     ? "menus/ports/falta"_i18n
                                                     : "menus/ports/no_elegida"_i18n;
                    const std::string der = est + (t > 0 ? "  ·  " + tamano(t) : "");
                    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                    nvgFontSize(vg, 16.0f);
                    nvgFillColor(vg, si == n ? this->a(oqb::verdeTexto()) : va ? texto : tenue);
                    nvgBeginPath(vg);
                    nvgText(vg, rx + rw - 16.0f, cy, der.c_str(), nullptr);
                    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                    ry += fh;
                }
                ry += 8.0f;
            }
            // Las partes.
            else if (this->port.partes.size() > 1) {
                const float fh = 46.0f;
                for (size_t i = 0; i < this->port.partes.size(); ++i) {
                    const ports::Parte& p = this->port.partes[i];
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, rx, ry, rw, fh - 6.0f, 11.0f);
                    nvgFillColor(vg, fondo);
                    nvgFill(vg);
                    const float cy = ry + (fh - 6.0f) / 2;
                    if (p.instalada)
                        check(vg, rx + 26.0f, cy, 13.0f, this->a(oqb::esmeralda()), this->a(nvgRGB(6, 20, 12)));
                    else {
                        nvgBeginPath(vg);
                        nvgCircle(vg, rx + 26.0f, cy, 13.0f);
                        nvgFillColor(vg, this->a(oqb::velo(22)));
                        nvgFill(vg);
                        nvgFontSize(vg, 14.0f);
                        nvgFillColor(vg, texto);
                        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                        nvgBeginPath(vg);
                        nvgText(vg, rx + 26.0f, cy + 1, std::to_string(i + 1).c_str(), nullptr);
                    }
                    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                    nvgFontSize(vg, 16.0f);
                    nvgFillColor(vg, texto);
                    nvgBeginPath(vg);
                    nvgText(vg, rx + 52.0f, cy, fmt::format("menus/ports/parte"_i18n, i + 1).c_str(), nullptr);
                    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                    const std::string der = (p.instalada ? "menus/ports/instalada"_i18n : "menus/ports/falta"_i18n) +
                                            (p.bytes > 0 ? "  ·  " + tamano(p.bytes) : "");
                    nvgFillColor(vg, p.instalada ? this->a(oqb::verdeTexto()) : texto);
                    nvgBeginPath(vg);
                    nvgText(vg, rx + rw - 16.0f, cy, der.c_str(), nullptr);
                    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                    ry += fh;
                }
                ry += 8.0f;
            }

            // El espacio (con secciones, el de lo marcado).
            const long long necesita = this->port.conSecciones() ? this->necesitaSeleccion() : this->necesita;
            if (necesita > 0 && this->libre >= 0) {
                const bool alcanza = necesita <= this->libre;
                const std::string t = alcanza ? fmt::format("menus/ports/espacio_ok"_i18n, tamano(necesita), tamano(this->libre))
                                              : fmt::format("menus/ports/espacio_no"_i18n, tamano(necesita), tamano(this->libre), tamano(necesita - this->libre));
                nvgFontSize(vg, 15.0f);
                nvgTextLineHeight(vg, 1.35f);
                nvgTextBoxBounds(vg, rx + 44.0f, ry + 14.0f, rw - 60.0f, t.c_str(), nullptr, caja);
                const float bh = caja[3] - (ry + 14.0f) + 28.0f;
                nvgBeginPath(vg);
                nvgRoundedRect(vg, rx, ry, rw, bh, 12.0f);
                nvgFillColor(vg, this->a(alcanza ? nvgRGBA(52, 213, 131, 26) : nvgRGBA(230, 185, 92, 34)));
                nvgFill(vg);
                if (alcanza)
                    check(vg, rx + 22.0f, ry + 24.0f, 10.0f, this->a(nvgRGBA(0, 0, 0, 0)), this->a(oqb::verdeTexto()));
                nvgFillColor(vg, this->a(alcanza ? oqb::verdeTexto() : oqb::doradoTexto()));
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                nvgBeginPath(vg);
                nvgTextBox(vg, rx + 44.0f, ry + 14.0f, rw - 60.0f, t.c_str(), nullptr);
                ry += bh + 16.0f;
            }

            // El boton (lo hace A).
            const std::string boton = this->textoBoton();
            nvgFontSize(vg, 19.0f);
            const float bw = nvgTextBounds(vg, 0, 0, boton.c_str(), nullptr, nullptr) + 90.0f, bh = 54.0f;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, rx, ry, bw, bh, 14.0f);
            nvgFillColor(vg, this->a(oqb::esmeralda()));
            nvgFill(vg);
            if (this->port.conSecciones() && this->cursor == (int)this->port.secciones.size()) {
                nvgBeginPath(vg);
                nvgRoundedRect(vg, rx - 5.0f, ry - 5.0f, bw + 10.0f, bh + 10.0f, 18.0f);
                nvgStrokeColor(vg, this->a(oqb::esmeralda()));
                nvgStrokeWidth(vg, 2.5f);
                nvgStroke(vg);
            }
            const NVGcolor sobre = this->a(oqb::oscuro() ? nvgRGB(6, 20, 12) : nvgRGB(255, 255, 255));
            nvgBeginPath(vg);
            nvgCircle(vg, rx + 30.0f, ry + bh / 2, 12.0f);
            nvgFillColor(vg, sobre);
            nvgFill(vg);
            nvgFontSize(vg, 14.0f);
            nvgFillColor(vg, this->a(oqb::esmeralda()));
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgBeginPath(vg);
            nvgText(vg, rx + 30.0f, ry + bh / 2 + 1, "A", nullptr);
            nvgFontSize(vg, 19.0f);
            nvgFillColor(vg, sobre);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgBeginPath(vg);
            nvgText(vg, rx + 54.0f, ry + bh / 2 + 1, boton.c_str(), nullptr);
            if (this->port.partes.size() > 1) {
                nvgFontSize(vg, 14.0f);
                nvgFillColor(vg, tenue);
                nvgBeginPath(vg);
                nvgText(vg, rx + bw + 18.0f, ry + bh / 2, "menus/ports/cancelar_nota"_i18n.c_str(), nullptr);
            }
            if (est == 1 && !this->port.nro.empty()) {
                nvgFontSize(vg, 15.0f);
                nvgTextLineHeight(vg, 1.35f);
                nvgFillColor(vg, tenue);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                nvgBeginPath(vg);
                nvgTextBox(vg, rx, ry + bh + 18.0f, rw, "menus/ports/acceso_directo_nota"_i18n.c_str(), nullptr);
            }
        }

    private:
        brls::StagedAppletFrame* frame;
        nlohmann::ordered_json nxlinks;
        ports::Port port;
        std::vector<int> aInstalar;
        long long necesita, libre;
        std::vector<bool> marcada;
        int cursor = 0;

        // Marca o desmarca (en un grupo: elige esa). Lo obligatorio no cambia.
        void tocar(int sec)
        {
            const auto& s = this->port.secciones[sec];
            if (!s.grupo.empty()) {
                for (int k = 0; k < (int)this->port.secciones.size(); ++k)
                    if (this->port.secciones[k].grupo == s.grupo) this->marcada[k] = k == sec;
            }
            else if (s.opcional)
                this->marcada[sec] = !this->marcada[sec];
            this->actualizarHint();
        }

        void actualizarHint()
        {
            if (!this->port.conSecciones()) return;
            const bool enFila = this->cursor < (int)this->port.secciones.size();
            this->updateActionHint(brls::Key::A, enFila ? "menus/ports/marcar"_i18n : this->textoBoton());
        }

        // Las partes a instalar: de lo marcado, lo que falta; si ya esta todo, todo de nuevo.
        std::vector<int> seleccion() const
        {
            std::vector<int> faltan, todas;
            for (int i = 0; i < (int)this->port.partes.size(); ++i) {
                if (!this->marcada[this->port.partes[i].seccion]) continue;
                todas.push_back(i);
                if (!this->port.partes[i].instalada) faltan.push_back(i);
            }
            return faltan.empty() ? todas : faltan;
        }

        long long necesitaSeleccion() const
        {
            long long n = 0;
            for (int i : this->seleccion()) {
                const auto& p = this->port.partes[i];
                const long long b = p.descomprimido > 0 ? p.descomprimido : p.bytes;
                if (b <= 0) return 0;
                n += b;
            }
            return n;
        }

        // Lo marcado: 0 nada instalado, 1 todo, 2 a medias.
        int estadoMarcado() const
        {
            int n = 0, si = 0;
            for (const auto& p : this->port.partes)
                if (this->marcada[p.seccion]) {
                    ++n;
                    si += p.instalada;
                }
            return si == 0 ? 0 : (si == n ? 1 : 2);
        }

        std::string textoBoton() const
        {
            if (this->port.conSecciones()) {
                const int e = this->estadoMarcado();
                return e == 1 ? "menus/ports/boton_reinstalar_marcado"_i18n : e == 2 ? "menus/ports/boton_faltan"_i18n : "menus/ports/boton_marcado"_i18n;
            }
            const int est = this->port.estado();
            if (est == 1) return "menus/ports/boton_reinstalar"_i18n;
            if (est == 2) return "menus/ports/boton_faltan"_i18n;
            if (this->port.partes.size() > 1) return fmt::format("menus/ports/boton_todas"_i18n, this->port.partes.size());
            return "menus/ports/boton_una"_i18n;
        }
    };

}  // namespace

namespace ports {

    long long Port::total() const
    {
        long long t = 0;
        for (const auto& p : this->partes) {
            if (p.bytes <= 0) return 0;  // sin medir: mejor no dar un total a medias
            t += p.bytes;
        }
        return t;
    }

    int Port::partesDe(int seccion) const
    {
        return std::count_if(this->partes.begin(), this->partes.end(), [seccion](const Parte& p) { return p.seccion == seccion; });
    }

    int Port::instaladasDe(int seccion) const
    {
        return std::count_if(this->partes.begin(), this->partes.end(), [seccion](const Parte& p) { return p.seccion == seccion && p.instalada; });
    }

    long long Port::totalDe(int seccion) const
    {
        long long t = 0;
        for (const auto& p : this->partes) {
            if (p.seccion != seccion) continue;
            if (p.bytes <= 0) return 0;
            t += p.bytes;
        }
        return t;
    }

    int Port::instaladas() const { return this->instaladasDe(0); }

    int Port::estado() const
    {
        if (this->conSecciones()) {
            /* Instalado: todo lo obligatorio y una de cada grupo. A medias: algo
               instalado. Lo opcional (DLC, actualizacion) no cuenta. */
            bool algo = false, completo = true;
            std::set<std::string> gruposCon, grupos;
            for (int sec = 0; sec < (int)this->secciones.size(); ++sec) {
                const int n = this->partesDe(sec), si = this->instaladasDe(sec);
                algo |= si > 0;
                const auto& s = this->secciones[sec];
                if (!s.grupo.empty()) {
                    grupos.insert(s.grupo);
                    if (n > 0 && si == n) gruposCon.insert(s.grupo);
                }
                else if (!s.opcional && si < n)
                    completo = false;
            }
            if (gruposCon.size() < grupos.size()) completo = false;
            return completo && algo ? 1 : algo ? 2 : 0;
        }
        const int n = this->instaladas();
        return n == 0 ? 0 : (n == this->partesDe(0) ? 1 : 2);
    }

    bool disponible(const nlohmann::ordered_json& nxlinks)
    {
        const auto sec = nxlinks.find("ports");
        return sec != nxlinks.end() && sec->is_object() && !sec->empty();
    }

    /* Las etapas que instalan esas partes, una detras de otra, y al final el
       acceso directo. Al terminar una parte de un grupo (Espanol / Ingles), las
       otras del grupo dejan de contar como instaladas: sus archivos se pisaron. */
    static std::vector<cola::Paso> pasosDe(const nlohmann::ordered_json& nxlinks, const Port& port, const std::vector<int>& aInstalar, std::shared_ptr<std::string> nroPath)
    {
        std::vector<cola::Paso> pasos;
        const int total = aInstalar.size();
        (void)total;
        for (int k = 0; k < total; ++k) {
            const int i = aInstalar[k];
            const std::string url = port.partes[i].url;
            const std::string fecha = appUpdates::published(nxlinks, url);
            const bool varias = port.partes.size() > 1;
            std::string titulo = varias ? fmt::format("menus/ports/bajando"_i18n, i + 1, port.partes.size()) : "menus/ports/bajando_una"_i18n;
            if (port.conSecciones()) {
                const int sec = port.partes[i].seccion;
                int n = 0, de = port.partesDe(sec);
                for (int j = 0; j <= i; ++j) n += port.partes[j].seccion == sec;
                const ports::Seccion& sc = port.secciones[sec];
                const std::string nombreSec = sc.grupo.empty() ? sc.nombre : sc.grupo + ": " + sc.nombre;
                titulo = de > 1 ? fmt::format("menus/ports/bajando_seccion"_i18n, nombreSec, n, de) : fmt::format("menus/ports/bajando_seccion_una"_i18n, nombreSec);
            }
            /* Una sola etapa por parte: se baja y se escribe en su lugar a la
               vez, sin guardar el zip (zip_vivo). Si el zip no se deja leer
               asi, el camino de siempre: bajarlo entero y descomprimirlo. */
            // Las partes de las otras secciones del mismo grupo (se pisan).
            std::vector<std::string> pisadas;
            if (port.conSecciones() && !port.secciones[port.partes[i].seccion].grupo.empty()) {
                const std::string& g = port.secciones[port.partes[i].seccion].grupo;
                for (const auto& otra : port.partes)
                    if (otra.seccion != port.partes[i].seccion && port.secciones[otra.seccion].grupo == g) pisadas.push_back(otra.url);
            }
            pasos.push_back({titulo, [url, fecha, nroPath, titulo, pisadas]() {
                actividad::paso(titulo);
                marcarPendiente(url, true);
                const zipvivo::Indice idx = zipvivo::leerIndice(url);
                if (idx.apto) {
                    if (nroPath->empty() && !idx.nro.empty()) *nroPath = idx.nro;
                    const long c = zipvivo::instalar(url, idx);
                    ProgressEvent::instance().setStatusCode(c == 200 ? 0 : c);
                }
                else {
                    brls::Logger::info("ports: {} sin instalar en vivo ({}); se baja el zip", url, idx.motivo);
                    util::downloadArchive(url, contentType::apps);
                    if (ProgressEvent::instance().getStatusCode() <= 399) {
                        if (util::isArchive(CUSTOM_FILENAME)) {
                            const std::string wrapper = extract::detectWrapperDir(CUSTOM_FILENAME);
                            const std::string nro = extract::findNroInArchive(CUSTOM_FILENAME, ROOT_PATH, wrapper);
                            if (nroPath->empty() && !nro.empty()) *nroPath = nro;
                        }
                        util::extractArchive(contentType::apps);
                    }
                    // El zip ocupa lo mismo que el juego: no se deja en la SD.
                    std::error_code ec;
                    std::filesystem::remove(CUSTOM_FILENAME, ec);
                }
                if (ProgressEvent::instance().getStatusCode() <= 399 && !ProgressEvent::instance().getInterupt()) {
                    appUpdates::recordInstalled(url, fecha);
                    marcarPendiente(url, false);
                    for (const auto& u : pisadas) appUpdates::forgetInstalled(u);
                }
            }});
        }
        return pasos;
    }

    void abrir(const nlohmann::ordered_json& nxlinks, const Port& port)
    {
        // Completo: se reinstala todo. Si no, solo lo que falta, en orden.
        std::vector<int> aInstalar;
        for (int i = 0; i < (int)port.partes.size(); ++i)
            if (port.estado() == 1 || !port.partes[i].instalada) aInstalar.push_back(i);

        /* Espacio: lo que ocupa descomprimido lo que se va a instalar (se
           instala mientras baja, sin el zip). Si el catalogo aun no tiene el
           tamano descomprimido, el del zip; sin ninguno no se calcula (al
           instalar igual se revisa parte por parte). */
        long long necesita = 0;
        for (int i : aInstalar) {
            const long long b = port.partes[i].descomprimido > 0 ? port.partes[i].descomprimido : port.partes[i].bytes;
            if (b <= 0) {
                necesita = 0;
                break;
            }
            necesita += b;
        }
        s64 libre = -1;
        if (R_FAILED(fs::getFreeStorageSD(libre))) libre = -1;

        brls::StagedAppletFrame* frame = new brls::StagedAppletFrame();
        frame->setTitle(port.nombre);
        Port copia = port;
        copia.imagen = -1;  // la ficha crea su propia textura
        frame->addStage(new PortDetalle(frame, nxlinks, copia, aInstalar, necesita, libre));
        brls::Application::pushView(frame);
    }

    namespace {
        std::string minusculas(std::string t)
        {
            std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::tolower(c); });
            return t;
        }

        /* Lo que un port nunca debe borrar aunque su zip lo traiga. retroarch/:
           los ports de PS1 traen el nucleo y las BIOS, que comparten RetroArch y
           los demas ports (re1.zip, 26-nov-2026). */
        bool intocable(const std::string& ruta)
        {
            const std::string r = minusculas(ruta);
            for (const char* p : {"retroarch/", "switch/retroarch/", "bootloader/", "emummc/", "nintendo/", "atmosphere/package3", "atmosphere/stratosphere", "atmosphere/config", "atmosphere/hosts", "atmosphere/kips", "hbmenu.nro", "payload.bin", "boot.dat", "boot.ini"})
                if (r.rfind(p, 0) == 0) return true;
            return r.find("..") != std::string::npos;
        }

        // Carpetas que se dejan aunque queden vacias.
        bool carpetaFija(const std::string& rel)
        {
            const std::string r = minusculas(rel);
            if (r.find('/') == std::string::npos) return true;  // las de la raiz: switch, atmosphere, config...
            return r == "atmosphere/contents" || r == "switch/.overlays" || r == "atmosphere/exefs_patches";
        }

        std::vector<std::string> archivosDe(const std::string& url, bool& ok)
        {
            std::vector<std::string> out;
            const zipvivo::Indice idx = zipvivo::leerIndice(url);
            if (idx.entradas.empty()) {
                ok = false;
                return out;
            }
            for (const auto& e : idx.entradas)
                if (!e.nombre.empty() && e.nombre.back() != '/') out.push_back(e.nombre);
            return out;
        }
    }  // namespace

    void eliminar(const nlohmann::ordered_json& nxlinks, const Port& port)
    {
        brls::StagedAppletFrame* frame = new brls::StagedAppletFrame();
        frame->setTitle(port.nombre);
        auto resultado = std::make_shared<std::string>();
        frame->addStage(new WorkerPage(frame, "menus/ports/eliminando"_i18n, [nxlinks, port, resultado]() {
            auto& progress = ProgressEvent::instance();
            actividad::paso(fmt::format("menus/ports/eliminando_de"_i18n, port.nombre));

            // Lo que traen los otros ports instalados se queda (archivos compartidos).
            std::set<std::string> deOtros;
            for (const auto& otro : leerPorts(nxlinks)) {
                if (otro.nombre == port.nombre) continue;
                for (const auto& pt : otro.partes) {
                    if (!pt.instalada) continue;
                    bool ok = true;
                    for (const auto& f : archivosDe(pt.url, ok)) deOtros.insert(minusculas(f));
                }
            }

            std::set<std::string> archivos;
            int sinIndice = 0;
            for (const auto& pt : port.partes) {
                bool ok = true;
                for (const auto& f : archivosDe(pt.url, ok)) archivos.insert(f);
                if (!ok) ++sinIndice;
            }

            progress.setTotalSteps(archivos.size() + 1);
            std::set<std::string> carpetas;
            int borrados = 0;
            for (const auto& rel : archivos) {
                progress.incrementStep(1);
                if (intocable(rel) || deOtros.count(minusculas(rel))) continue;
                std::error_code ec;
                if (std::filesystem::remove(std::string(ROOT_PATH) + rel, ec)) ++borrados;
                for (size_t p = rel.rfind('/'); p != std::string::npos && p > 0; p = rel.rfind('/', p - 1)) carpetas.insert(rel.substr(0, p));
            }
            // Las carpetas que quedaron vacias, de la mas honda a la de arriba.
            std::vector<std::string> orden(carpetas.begin(), carpetas.end());
            std::sort(orden.begin(), orden.end(), [](const std::string& a, const std::string& b) { return std::count(a.begin(), a.end(), '/') > std::count(b.begin(), b.end(), '/'); });
            for (const auto& c : orden) {
                if (carpetaFija(c)) continue;
                std::error_code ec;
                if (std::filesystem::is_empty(std::string(ROOT_PATH) + c, ec) && !ec) std::filesystem::remove(std::string(ROOT_PATH) + c, ec);
            }

            // Sus accesos directos del menu HOME.
            std::vector<std::string> nros = port.accesos;
            if (!port.nro.empty()) nros.push_back(port.nro);
            for (const auto& pt : port.partes) {
                const std::string n = nroDe(nxlinks, pt.url);
                if (!n.empty()) nros.push_back(n);
            }
            for (const auto& n : nros) {
                const std::string archivo = n.substr(n.rfind('/') + 1);
                for (const auto& cand : util::nroCandidatePaths(archivo)) fwd::remove(cand);
                fwd::remove(n);
            }

            // Mientras no se pudo leer algun indice, se queda marcado como instalado
            // para poder intentarlo de nuevo.
            if (sinIndice == 0) {
                for (const auto& pt : port.partes) {
                    appUpdates::forgetInstalled(pt.url);
                    marcarPendiente(pt.url, false);
                }
                *resultado = fmt::format("menus/ports/eliminado"_i18n, port.nombre);
            }
            else
                *resultado = "menus/ports/eliminar_sin_red"_i18n;
            (void)borrados;
            progress.setStep(progress.getMax());
        }));
        frame->addStage((new ConfirmPage_Deferred(frame, resultado))->returnToList());
        brls::Application::pushView(frame);
    }

    void instalar(const nlohmann::ordered_json& nxlinks, const Port& port, const std::vector<int>& aInstalar)
    {
        /* A la cola (5.0.0): su pantalla de siempre, y con B se sigue usando la
           app. El acceso directo se ofrece al terminar (main.cpp, siguienteOferta). */
        auto nroPath = std::make_shared<std::string>();
        const std::vector<std::string> propios = port.accesos;
        const std::string nombre = port.nombre;
        std::vector<cola::Paso> pasos = pasosDe(nxlinks, port, aInstalar, nroPath);
        // La pantalla de siempre, mirando ese trabajo de la cola.
        brls::StagedAppletFrame* frame = new brls::StagedAppletFrame();
        frame->setTitle(port.nombre);
        frame->addStage(WorkerPage::deCola(frame, [nombre, pasos, nroPath, propios]() {
            return cola::agregar(nombre, pasos, [nroPath, propios]() {
                if (!propios.empty()) return propios;
                return nroPath->empty() ? std::vector<std::string>{} : std::vector<std::string>{*nroPath};
            });
        }));
        frame->addStage((new ConfirmPage_Done(frame, "menus/common/all_done"_i18n))->returnToList());
        brls::Application::pushView(frame);
    }


}  // namespace ports

PortsPage::PortsPage(const nlohmann::ordered_json& nxlinks) : nxlinks(nxlinks)
{
    this->releer();

    this->registerAction("", brls::Key::DUP, [this] { this->mover(-this->columnas); return true; }, true);
    this->registerAction("", brls::Key::DDOWN, [this] { this->mover(this->columnas); return true; }, true);
    this->registerAction("", brls::Key::DRIGHT, [this] {
        if ((this->elegido + 1) % this->columnas != 0) this->mover(1);
        return true;
    }, true);
    // En la primera columna, izquierda vuelve al menu.
    this->registerAction("", brls::Key::DLEFT, [this] {
        if (this->elegido % this->columnas == 0) return false;
        this->mover(-1);
        return true;
    }, true);
    this->registerAction("", brls::Key::L, [this] {
        this->chip = (this->chip + 3) % 4;
        this->elegido = this->primeraFila = 0;
        return true;
    }, true);
    this->registerAction("menus/ports/hint_filtro"_i18n, brls::Key::R, [this] {
        this->chip = (this->chip + 1) % 4;
        this->elegido = this->primeraFila = 0;
        return true;
    });
    this->registerAction("menus/ports/hint_buscar"_i18n, brls::Key::X, [this] {
        std::string q;
        if (brls::Swkbd::openForText([&q](std::string t) { q = t; }, "menus/ports/buscar_titulo"_i18n, "", 48, this->filtro, 0, "menus/nextendo/search_ok"_i18n, ""))
            this->filtro = q;
        this->elegido = this->primeraFila = 0;
        return true;
    });
    this->registerAction("menus/ports/hint_orden"_i18n, brls::Key::Y, [this] {
        this->orden = (this->orden + 1) % 4;
        this->elegido = this->primeraFila = 0;
        return true;
    });
    this->registerAction("menus/ports/hint_ver"_i18n, brls::Key::A, [this] {
        const auto v = this->visibles();
        if (this->elegido >= 0 && this->elegido < (int)v.size()) ports::abrir(this->nxlinks, this->todos[v[this->elegido]]);
        return true;
    });
}

PortsPage::~PortsPage()
{
    NVGcontext* vg = brls::Application::getNVGContext();
    for (auto& p : this->todos)
        if (p.imagen > 0) nvgDeleteImage(vg, p.imagen);
}

void PortsPage::releer()
{
    this->genInstalaciones = appUpdates::generacionInstalaciones();
    std::vector<ports::Port> nuevos = leerPorts(this->nxlinks);
    // Las portadas ya creadas se conservan.
    for (auto& n : nuevos)
        for (auto& v : this->todos)
            if (v.nombre == n.nombre) {
                n.imagen = v.imagen;
                v.imagen = -1;
            }
    NVGcontext* vg = brls::Application::getNVGContext();
    for (auto& v : this->todos)
        if (v.imagen > 0) nvgDeleteImage(vg, v.imagen);
    this->todos = std::move(nuevos);
}

std::vector<int> PortsPage::visibles() const
{
    std::vector<int> out;
    const std::string q = minusculas(this->filtro);
    for (int i = 0; i < (int)this->todos.size(); ++i) {
        const ports::Port& p = this->todos[i];
        if (!q.empty() && minusculas(p.nombre).find(q) == std::string::npos) continue;
        const int e = p.estado();
        if (this->chip == 1 && e != 1) continue;
        if (this->chip == 2 && e != 2) continue;
        if (this->chip == 3 && p.partesDe(0) < 2) continue;
        out.push_back(i);
    }
    // Lo que quedo a medias primero: es lo que hay que terminar.
    /* El orden elegido con Y, en cualquier filtro. Sin tamano o fecha conocidos
       van al final, en el orden del catalogo. */
    std::stable_sort(out.begin(), out.end(), [this](int a, int b) {
        const ports::Port& x = this->todos[a];
        const ports::Port& y = this->todos[b];
        switch (this->orden) {
            case 1:
            case 2: {
                const long long tx = x.total(), ty = y.total();
                if ((tx > 0) != (ty > 0)) return tx > 0;
                return this->orden == 1 ? tx > ty : tx < ty;
            }
            case 3: return minusculas(x.nombre) < minusculas(y.nombre);
            default: return x.fecha > y.fecha;
        }
    });
    return out;
}

void PortsPage::mover(int delta)
{
    const int n = this->visibles().size();
    if (n == 0) return;
    this->elegido = std::clamp(this->elegido + delta, 0, n - 1);
}

int PortsPage::portada(NVGcontext* vg, ports::Port& p)
{
    if (p.imagen < 0) p.imagen = crearPortada(vg, this->nxlinks, p);
    return p.imagen;
}

void PortsPage::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
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

    const float lx = x + 36.0f, lw = width - 72.0f;
    float ly = y + 30.0f;

    // Titulo y subtitulo.
    nvgFontFaceId(vg, this->fuente >= 0 ? this->fuente : regular);
    nvgFontSize(vg, 40.0f);
    nvgTextLetterSpacing(vg, 1.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, texto);
    nvgBeginPath(vg);
    nvgText(vg, lx, ly + 20.0f, "menus/ports/titulo"_i18n.c_str(), nullptr);
    nvgTextLetterSpacing(vg, 0.0f);
    nvgFontFaceId(vg, regular);
    nvgFontSize(vg, 16.0f);
    nvgFillColor(vg, tenue);
    nvgBeginPath(vg);
    // A la derecha del subtitulo, el orden en uso (Y lo cambia).
    {
        const char* claves[4] = {"menus/ports/orden_recientes", "menus/ports/orden_pesados", "menus/ports/orden_livianos", "menus/ports/orden_nombre"};
        const std::string orden = fmt::format("menus/ports/orden"_i18n, i18n::getStr(claves[this->orden]));
        nvgFontSize(vg, 15.0f);
        const float ow = nvgTextBounds(vg, 0, 0, orden.c_str(), nullptr, nullptr);
        nvgFontSize(vg, 16.0f);
        nvgFillColor(vg, tenue);
        nvgBeginPath(vg);
        nvgText(vg, lx, ly + 58.0f, cortar(vg, fmt::format("menus/ports/sub"_i18n, this->todos.size()), lw - ow - 40.0f).c_str(), nullptr);
        nvgFontSize(vg, 15.0f);
        nvgFillColor(vg, texto);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgBeginPath(vg);
        nvgText(vg, lx + lw, ly + 58.0f, orden.c_str(), nullptr);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    }
    ly += 84.0f;

    // Buscador y chips en una fila.
    int instalados = 0, aMedias = 0, enPartes = 0;
    for (const auto& p : this->todos) {
        const int e = p.estado();
        instalados += e == 1;
        aMedias += e == 2;
        enPartes += p.partesDe(0) > 1;
    }
    {
        const float bw = 250.0f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, lx, ly, bw, 40.0f, 12.0f);
        nvgFillColor(vg, fondo);
        nvgFill(vg);
        nvgStrokeColor(vg, borde);
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);
        nvgFontSize(vg, 15.0f);
        nvgFillColor(vg, this->filtro.empty() ? tenue : texto);
        nvgBeginPath(vg);
        const std::string b = this->filtro.empty() ? fmt::format("menus/ports/buscar"_i18n, this->todos.size()) : fmt::format("menus/ports/buscando"_i18n, this->filtro);
        nvgText(vg, lx + 14.0f, ly + 20.0f, cortar(vg, b, bw - 28.0f).c_str(), nullptr);

        const std::string chips[4] = {fmt::format("menus/ports/chip_todos"_i18n, this->todos.size()), fmt::format("menus/ports/chip_instalados"_i18n, instalados),
                                      fmt::format("menus/ports/chip_medias"_i18n, aMedias), fmt::format("menus/ports/chip_partes"_i18n, enPartes)};
        float cx = lx + bw + 12.0f;
        nvgFontSize(vg, 14.0f);
        for (int i = 0; i < 4; ++i) {
            const float w = nvgTextBounds(vg, 0, 0, chips[i].c_str(), nullptr, nullptr) + 26.0f;
            const bool sel = i == this->chip;
            const bool ambar = i == 2 && aMedias > 0;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx, ly + 4.0f, w, 32.0f, 16.0f);
            if (sel) {
                nvgFillColor(vg, texto);
                nvgFill(vg);
            }
            else if (ambar) {
                nvgFillColor(vg, this->a(nvgRGBA(230, 185, 92, oqb::oscuro() ? 40 : 70)));
                nvgFill(vg);
            }
            else {
                nvgStrokeColor(vg, borde);
                nvgStrokeWidth(vg, 1.0f);
                nvgStroke(vg);
            }
            nvgFillColor(vg, sel ? this->a(ctx->theme->backgroundColorRGB) : (ambar ? this->a(oqb::doradoTexto()) : tenue));
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgBeginPath(vg);
            nvgText(vg, cx + w / 2, ly + 21.0f, chips[i].c_str(), nullptr);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            cx += w + 8.0f;
        }
        ly += 60.0f;
    }

    // La grilla.
    const auto v = this->visibles();
    if (this->elegido >= (int)v.size()) this->elegido = std::max(0, (int)v.size() - 1);
    const float gap = 18.0f;
    const float celda = (lw - (this->columnas - 1) * gap) / this->columnas;
    const float lado = std::min(celda, 150.0f);
    const float filaH = lado + 54.0f;
    const int caben = std::max(1, (int)((y + height - ly) / filaH));
    const int filaElegida = this->elegido / this->columnas;
    if (filaElegida < this->primeraFila) this->primeraFila = filaElegida;
    if (filaElegida >= this->primeraFila + caben) this->primeraFila = filaElegida - caben + 1;

    nvgSave(vg);
    nvgScissor(vg, x, ly - 8.0f, width, y + height - ly + 8.0f);
    const int desde = this->primeraFila * this->columnas;
    // Una fila de mas, cortada abajo: deja ver que hay mas.
    for (int idx = desde; idx < (int)v.size() && idx < desde + (caben + 1) * this->columnas; ++idx) {
        ports::Port& p = this->todos[v[idx]];
        const int col = idx % this->columnas, fila = idx / this->columnas - this->primeraFila;
        const float cx = lx + col * (celda + gap), cy = ly + fila * filaH;
        const int img = this->portada(vg, p);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, cx, cy, lado, lado, 14.0f);
        if (img > 0)
            nvgFillPaint(vg, nvgImagePattern(vg, cx, cy, lado, lado, 0, img, this->getAlpha()));
        else
            nvgFillColor(vg, this->a(oqb::velo(30)));
        nvgFill(vg);
        if (idx == this->elegido && conFoco) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx - 5.0f, cy - 5.0f, lado + 10.0f, lado + 10.0f, 18.0f);
            nvgStrokeColor(vg, foco);
            nvgStrokeWidth(vg, 3.0f);
            nvgStroke(vg);
        }
        // Marcas sobre la portada.
        if (p.actualizacion)
            pildora(vg, cx + 7.0f, cy + 17.0f, "menus/ports/actualizacion"_i18n, 11.0f, this->a(nvgRGBA(10, 10, 8, 210)), this->a(nvgRGB(143, 211, 240)), 20.0f);
        else if (p.partesDe(0) > 1)
            pildora(vg, cx + 7.0f, cy + 17.0f, fmt::format("menus/ports/partes"_i18n, p.partesDe(0)), 11.0f, this->a(nvgRGBA(10, 10, 8, 210)), this->a(nvgRGB(242, 209, 107)), 20.0f);
        const int e = p.estado();
        if (e == 1) check(vg, cx + lado - 20.0f, cy + lado - 20.0f, 13.0f, this->a(oqb::esmeralda()), this->a(nvgRGB(6, 20, 12)));
        if (e == 2) {
            const int faltan = p.partesDe(0) - p.instaladas();
            nvgBeginPath(vg);
            nvgRoundedRectVarying(vg, cx, cy + lado - 26.0f, lado, 26.0f, 0, 0, 14.0f, 14.0f);
            nvgFillColor(vg, this->a(nvgRGBA(230, 185, 92, 235)));
            nvgFill(vg);
            nvgFontSize(vg, 12.0f);
            nvgFillColor(vg, this->a(nvgRGB(29, 20, 4)));
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgBeginPath(vg);
            const std::string t = faltan == 1 ? "menus/ports/falta_una"_i18n : fmt::format("menus/ports/faltan"_i18n, faltan);
            nvgText(vg, cx + lado / 2, cy + lado - 12.0f, t.c_str(), nullptr);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        }
        // Nombre y tamano.
        nvgFontSize(vg, 15.0f);
        nvgFillColor(vg, texto);
        nvgBeginPath(vg);
        nvgText(vg, cx, cy + lado + 16.0f, cortar(vg, p.nombre, lado).c_str(), nullptr);
        const std::string meta = p.partesDe(0) > 1 ? (p.totalDe(0) > 0 ? fmt::format("menus/ports/meta_partes"_i18n, tamano(p.totalDe(0)), p.partesDe(0))
                                                                        : fmt::format("menus/ports/partes"_i18n, p.partesDe(0)))
                                                     : tamano(p.total());
        nvgFontSize(vg, 13.0f);
        nvgFillColor(vg, tenue);
        nvgBeginPath(vg);
        nvgText(vg, cx, cy + lado + 36.0f, meta.c_str(), nullptr);
    }
    if (v.empty()) {
        nvgFontSize(vg, 17.0f);
        nvgFillColor(vg, tenue);
        nvgBeginPath(vg);
        nvgText(vg, lx + 4.0f, ly + 20.0f, "menus/ports/vacio"_i18n.c_str(), nullptr);
    }
    nvgRestore(vg);
}
