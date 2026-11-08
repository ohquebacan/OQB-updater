#include "trucos_page.hpp"

#include <borealis.hpp>
#include <switch.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "constants.hpp"
#include "download.hpp"
#include "download_cheats_page.hpp"
#include "extract.hpp"
#include "list_download_tab.hpp"
#include "utils.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {

    enum Estado { CON_TRUCOS = 0, OTRA_VERSION, VERSION_DESCONOCIDA, SIN_TRUCOS };

    struct Version {
        u32 numero = 0;
        std::string bid;
        bool conTrucos = false;
    };

    struct Juego {
        u64 tid = 0;
        std::string nombre, visible;  // visible: la version que muestra el menu HOME
        u32 version = 0;
        std::string bid;              // build de la version instalada, si la base lo conoce
        int trucos = 0;               // trucos en el archivo de esa build
        std::vector<Version> versiones;  // de la base, de la mas nueva a la mas vieja
        u32 ultima = 0;
        bool hayUltima = false;
        std::vector<uint8_t> icono;
        Estado estado = SIN_TRUCOS;
    };

    std::string sinEspacios(std::string s)
    {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        return s;
    }

    const std::string CACHE = std::string(CONFIG_PATH) + "trucos_versiones.json";
    const std::string MARCA = std::string(CONFIG_PATH) + "trucos_versiones.ver";

    /* versions.json de la base: de la SD si es de la base publicada, si no se
       baja (una vez) y se guarda. Sin conexion vale lo guardado aunque sea viejo. */
    std::string versionesDeLaBase()
    {
        const std::string publicada = sinEspacios(util::getCheatsVersion());
        std::error_code ec;
        const bool hayCache = std::filesystem::exists(CACHE, ec);
        auto leer = []() {
            std::ifstream f(CACHE, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        };
        if (hayCache && (publicada.empty() || publicada == sinEspacios(util::readFile(MARCA))))
            return leer();
        std::string texto;
        if (download::textoCorto(LOOKUP_TABLE_URL, texto, 40, 16u << 20) == 200 && texto.size() > 1000 && texto[0] == '{') {
            const std::string tmp = CACHE + ".tmp";
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                f << texto;
            }
            std::filesystem::rename(tmp, CACHE, ec);
            std::ofstream(MARCA, std::ios::trunc) << publicada;
            return texto;
        }
        return hayCache ? leer() : std::string();
    }

    /* El objeto de un juego dentro de versions.json, sin armar el json entero
       (son 3,6 MB: armarlo completo pesa decenas de MB en memoria). */
    nlohmann::json objetoDe(const std::string& todo, const std::string& tid)
    {
        const auto clave = todo.find("\"" + tid + "\"");
        if (clave == std::string::npos) return {};
        const auto abre = todo.find('{', clave);
        if (abre == std::string::npos) return {};
        const auto cierra = todo.find('}', abre);
        if (cierra == std::string::npos) return {};
        return nlohmann::json::parse(todo.begin() + abre, todo.begin() + cierra + 1, nullptr, false);
    }

    int contarTrucos(const std::string& ruta)
    {
        std::ifstream f(ruta);
        std::string linea;
        int n = 0;
        while (std::getline(f, linea)) {
            const auto p = linea.find_first_not_of(" \t");
            if (p != std::string::npos && linea[p] == '[' && linea.find(']', p) != std::string::npos) ++n;
        }
        return n;
    }

    // Lee los juegos instalados en un hilo. Solo llena datos.
    class Lectura
    {
    public:
        std::atomic<bool> listo{false}, cancelar{false};
        std::atomic<int> hechos{0}, total{0};
        std::vector<Juego> juegos;  // se puede leer cuando listo == true
        bool baseBajada = false;

        void empezar()
        {
            this->hilo = std::thread([this] { this->correr(); });
        }

        ~Lectura()
        {
            this->cancelar = true;
            if (this->hilo.joinable()) this->hilo.join();
        }

    private:
        std::thread hilo;

        void correr()
        {
            std::vector<Juego> out;
            this->baseBajada = !sinEspacios(util::readFile(CHEATS_VERSION)).empty();

            constexpr s32 MAXIMO = 64000;
            std::vector<NsApplicationRecord> registros(MAXIMO);
            s32 cuantos = 0;
            if (R_FAILED(nsListApplicationRecord(registros.data(), MAXIMO, 0, &cuantos))) cuantos = 0;
            this->total = cuantos;

            const std::string base = versionesDeLaBase();
            auto control = std::make_unique<NsApplicationControlData>();
            for (s32 i = 0; i < cuantos && !this->cancelar; ++i) {
                Juego j;
                j.tid = registros[i].application_id;
                const std::string tid = util::formatApplicationId(j.tid);

                u64 tam = 0;
                if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, j.tid, control.get(), sizeof(NsApplicationControlData), &tam)) &&
                    tam >= sizeof(control->nacp)) {
                    NacpLanguageEntry* idioma = nullptr;
                    if (R_SUCCEEDED(nacpGetLanguageEntry(&control->nacp, &idioma)) && idioma)
                        j.nombre = std::string(idioma->name, strnlen(idioma->name, sizeof(idioma->name)));
                    j.visible = std::string(control->nacp.display_version, strnlen(control->nacp.display_version, sizeof(control->nacp.display_version)));
                    if (tam > sizeof(control->nacp)) j.icono.assign(control->icon, control->icon + (tam - sizeof(control->nacp)));
                }
                if (j.nombre.empty()) j.nombre = tid;
                j.version = cheats_util::GetVersion(j.tid);

                // Builds con trucos: los archivos de la base en la SD.
                std::set<std::string> conTrucos;
                std::error_code ec;
                const std::string carpeta = util::getContentsPath() + tid + "/cheats/";
                for (auto it = std::filesystem::directory_iterator(carpeta, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
                    const std::string stem = util::upperCase(it->path().stem().string());
                    if (extract::isBID(stem)) conTrucos.insert(stem);
                }

                const nlohmann::json obj = base.empty() ? nlohmann::json() : objetoDe(base, tid);
                if (obj.is_object()) {
                    for (const auto& [k, v] : obj.items()) {
                        if (k == "latest") {
                            if (v.is_number_unsigned()) {
                                j.ultima = v.get<u32>();
                                j.hayUltima = true;
                            }
                            continue;
                        }
                        if (!v.is_string()) continue;
                        Version ver;
                        ver.numero = static_cast<u32>(std::strtoul(k.c_str(), nullptr, 10));
                        ver.bid = util::upperCase(v.get<std::string>());
                        ver.conTrucos = conTrucos.count(ver.bid) > 0;
                        if (ver.numero == j.version) j.bid = ver.bid;
                        j.versiones.push_back(ver);
                    }
                    std::sort(j.versiones.begin(), j.versiones.end(), [](const Version& a, const Version& b) { return a.numero > b.numero; });
                }

                if (!j.bid.empty() && conTrucos.count(j.bid)) {
                    j.estado = CON_TRUCOS;
                    j.trucos = contarTrucos(carpeta + j.bid + ".txt");
                }
                else if (conTrucos.empty())
                    j.estado = SIN_TRUCOS;
                else
                    j.estado = j.bid.empty() ? VERSION_DESCONOCIDA : OTRA_VERSION;

                out.push_back(std::move(j));
                this->hechos = i + 1;
            }
            std::sort(out.begin(), out.end(), [](const Juego& a, const Juego& b) {
                const int pa = a.estado == CON_TRUCOS ? 0 : (a.estado == SIN_TRUCOS ? 2 : 1);
                const int pb = b.estado == CON_TRUCOS ? 0 : (b.estado == SIN_TRUCOS ? 2 : 1);
                return pa != pb ? pa < pb : a.nombre < b.nombre;
            });
            this->juegos = std::move(out);
            this->listo = true;
        }
    };

    // "v1.3.0" para la instalada; para las demas, el numero de actualizacion.
    std::string nombreVersion(const Juego& j, u32 numero)
    {
        if (numero == j.version && !j.visible.empty())
            return (std::isdigit(static_cast<unsigned char>(j.visible[0])) ? "v" : "") + j.visible;
        if (numero == 0) return "menus/trucos/juego_base"_i18n;
        return fmt::format("menus/trucos/actualizacion"_i18n, numero / 0x10000);
    }

    std::string conTrucosLista(const Juego& j)
    {
        std::string out;
        int n = 0;
        for (const auto& v : j.versiones) {
            if (!v.conTrucos) continue;
            if (n == 3) {
                out += ", …";
                break;
            }
            out += (n++ ? ", " : "") + nombreVersion(j, v.numero);
        }
        return out;
    }

    std::string cortar(NVGcontext* vg, const std::string& texto, float ancho)
    {
        if (nvgTextBounds(vg, 0, 0, texto.c_str(), nullptr, nullptr) <= ancho) return texto;
        std::string s = texto;
        while (!s.empty()) {
            do s.pop_back();
            while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80);  // caracter entero
            if (!s.empty()) s.pop_back();
            const std::string prueba = s + "…";
            if (nvgTextBounds(vg, 0, 0, prueba.c_str(), nullptr, nullptr) <= ancho) return prueba;
        }
        return "…";
    }

    struct Colores {
        NVGcolor fondo, texto;
    };

    Colores coloresDe(Estado e)
    {
        switch (e) {
            case CON_TRUCOS: return {nvgRGBA(52, 213, 131, 46), nvgRGB(157, 234, 154)};
            case OTRA_VERSION:
            case VERSION_DESCONOCIDA: return {nvgRGBA(230, 185, 92, 46), nvgRGB(230, 185, 92)};
            default: return {nvgRGBA(255, 255, 255, 20), nvgRGB(160, 160, 160)};
        }
    }

    std::string etiquetaDe(Estado e)
    {
        switch (e) {
            case CON_TRUCOS: return "menus/trucos/pill_ok"_i18n;
            case OTRA_VERSION: return "menus/trucos/pill_otra"_i18n;
            case VERSION_DESCONOCIDA: return "menus/trucos/pill_desconocida"_i18n;
            default: return "menus/trucos/pill_sin"_i18n;
        }
    }

    float pildora(NVGcontext* vg, float x, float cy, const std::string& texto, const Colores& c, float alpha, float tam = 15.0f)
    {
        nvgFontSize(vg, tam);
        const float w = nvgTextBounds(vg, 0, 0, texto.c_str(), nullptr, nullptr) + 22.0f, h = tam + 11.0f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, cy - h / 2, w, h, h / 2);
        nvgFillColor(vg, nvgRGBAf(c.fondo.r, c.fondo.g, c.fondo.b, c.fondo.a * alpha));
        nvgFill(vg);
        nvgFillColor(vg, nvgRGBAf(c.texto.r, c.texto.g, c.texto.b, alpha));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgBeginPath(vg);
        nvgText(vg, x + w / 2, cy + 1, texto.c_str(), nullptr);
        return w;
    }

    class Vista : public brls::View
    {
        std::unique_ptr<Lectura> lectura;
        std::string baseLeida;  // CHEATS_VERSION cuando se empezo a leer
        int filtro = 0;         // 0 todos, 1 listos, 2 otra version, 3 sin trucos
        int elegido = 0, primero = 0;
        std::map<u64, int> iconos;

        std::vector<int> visibles() const
        {
            std::vector<int> out;
            if (!this->lectura || !this->lectura->listo) return out;
            const auto& js = this->lectura->juegos;
            for (int i = 0; i < (int)js.size(); ++i) {
                const Estado e = js[i].estado;
                if (this->filtro == 0 || (this->filtro == 1 && e == CON_TRUCOS) || (this->filtro == 2 && (e == OTRA_VERSION || e == VERSION_DESCONOCIDA)) ||
                    (this->filtro == 3 && e == SIN_TRUCOS))
                    out.push_back(i);
            }
            return out;
        }

        const Juego* actual() const
        {
            const auto v = this->visibles();
            return this->elegido >= 0 && this->elegido < (int)v.size() ? &this->lectura->juegos[v[this->elegido]] : nullptr;
        }

        void leer()
        {
            this->borrarIconos();
            this->baseLeida = sinEspacios(util::readFile(CHEATS_VERSION));
            this->elegido = this->primero = 0;
            this->lectura = std::make_unique<Lectura>();
            this->lectura->empezar();
        }

        void borrarIconos()
        {
            NVGcontext* vg = brls::Application::getNVGContext();
            for (const auto& [tid, img] : this->iconos)
                if (img > 0) nvgDeleteImage(vg, img);
            this->iconos.clear();
        }

        void mover(int d)
        {
            const int n = this->visibles().size();
            if (!n) return;
            this->elegido = std::clamp(this->elegido + d, 0, n - 1);
        }

    public:
        Vista()
        {
            this->leer();
            this->registerAction("", brls::Key::DUP, [this] { this->mover(-1); return true; }, true);
            this->registerAction("", brls::Key::DDOWN, [this] { this->mover(1); return true; }, true);
            this->registerAction("", brls::Key::L, [this] {
                this->filtro = (this->filtro + 3) % 4;
                this->elegido = this->primero = 0;
                return true;
            }, true);
            this->registerAction("menus/trucos/filtro"_i18n, brls::Key::R, [this] {
                this->filtro = (this->filtro + 1) % 4;
                this->elegido = this->primero = 0;
                return true;
            });
            this->registerAction("menus/trucos/como"_i18n, brls::Key::Y, [] {
                util::showDialogBoxInfo("menus/trucos/como_texto"_i18n);
                return true;
            });
            this->registerAction("menus/trucos/base"_i18n, brls::Key::X, [] {
                brls::AppletFrame* frame = new brls::AppletFrame(true, true);
                frame->setContentView(new ListDownloadTab(contentType::cheats));
                brls::PopupFrame::open("menus/main/download_cheats"_i18n, frame, "", "");
                return true;
            });
            this->registerAction("menus/trucos/ver"_i18n, brls::Key::A, [this] {
                const Juego* j = this->actual();
                if (!j) return true;
                if (j->estado == CON_TRUCOS)
                    cheats_util::ShowCheatSheet(j->tid, j->bid, j->nombre);
                else
                    cheats_util::ShowCheatFiles(j->tid, j->nombre);
                return true;
            });
        }

        ~Vista() override
        {
            this->lectura.reset();  // espera al hilo
            this->borrarIconos();
        }

        // Al volver de bajar la base, se vuelve a leer con los trucos nuevos.
        void willAppear(bool resetState) override
        {
            if (this->lectura && this->lectura->listo && sinEspacios(util::readFile(CHEATS_VERSION)) != this->baseLeida) this->leer();
            brls::View::willAppear(resetState);
        }

        brls::View* getDefaultFocus() override { return this; }
        // Sin el recuadro de foco de borealis alrededor de toda la pantalla.
        void onFocusGained() override { this->focused = true; }
        void onFocusLost() override { this->focused = false; }

        void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override
        {
            const NVGcolor texto = this->a(ctx->theme->textColor);
            const NVGcolor tenue = this->a(ctx->theme->descriptionColor);
            nvgFontFaceId(vg, ctx->fontStash->regular);

            if (!this->lectura || !this->lectura->listo) {
                nvgFontSize(vg, 24.0f);
                nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                nvgFillColor(vg, tenue);
                const std::string t = fmt::format("menus/trucos/leyendo"_i18n, this->lectura ? this->lectura->hechos.load() : 0, this->lectura ? this->lectura->total.load() : 0);
                nvgBeginPath(vg);
                nvgText(vg, x + width / 2.0f, y + height / 2.0f, t.c_str(), nullptr);
                return;
            }

            const auto& js = this->lectura->juegos;
            const auto vis = this->visibles();
            const float izq = x + 30.0f, anchoIzq = width * 0.54f, der = x + 30.0f + anchoIzq + 30.0f, anchoDer = x + width - 30.0f - der;

            // La base de trucos.
            {
                const std::string publicada = sinEspacios(util::getCheatsVersion());
                const std::string tengo = this->baseLeida;
                Estado e = CON_TRUCOS;
                std::string titulo, sub;
                if (tengo.empty()) {
                    e = OTRA_VERSION;
                    titulo = "menus/trucos/base_falta"_i18n;
                    sub = "menus/trucos/base_falta_sub"_i18n;
                }
                else if (!publicada.empty() && publicada != tengo) {
                    e = OTRA_VERSION;
                    titulo = fmt::format("menus/trucos/base_nueva"_i18n, publicada);
                    sub = fmt::format("menus/trucos/base_tienes"_i18n, tengo);
                }
                else {
                    titulo = "menus/trucos/base_al_dia"_i18n;
                    sub = fmt::format("menus/trucos/base_tienes"_i18n, tengo);
                }
                const Colores c = coloresDe(e);
                const float by = y + 18.0f, bh = 70.0f;
                nvgBeginPath(vg);
                nvgRoundedRect(vg, izq, by, anchoIzq, bh, 12.0f);
                nvgFillColor(vg, this->a(c.fondo));
                nvgFill(vg);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                nvgFillColor(vg, this->a(c.texto));
                nvgFontSize(vg, 21.0f);
                nvgBeginPath(vg);
                nvgText(vg, izq + 20.0f, by + 12.0f, titulo.c_str(), nullptr);
                nvgFontSize(vg, 16.0f);
                nvgBeginPath(vg);
                nvgText(vg, izq + 20.0f, by + 42.0f, cortar(vg, sub, anchoIzq - 40.0f).c_str(), nullptr);
            }

            // Filtros.
            {
                int cuenta[4] = {(int)js.size(), 0, 0, 0};
                for (const auto& j : js) cuenta[j.estado == CON_TRUCOS ? 1 : (j.estado == SIN_TRUCOS ? 3 : 2)]++;
                const char* claves[] = {"menus/trucos/f_todos", "menus/trucos/f_listos", "menus/trucos/f_otra", "menus/trucos/f_sin"};
                float cx = izq;
                for (int f = 0; f < 4; ++f) {
                    const std::string t = fmt::format("{} · {}", i18n::getStr(claves[f]), cuenta[f]);
                    const Colores c = f == this->filtro ? Colores{ctx->theme->textColor, ctx->theme->backgroundColorRGB} : Colores{nvgRGBA(255, 255, 255, 18), ctx->theme->descriptionColor};
                    cx += pildora(vg, cx, y + 118.0f, t, c, this->alpha) + 10.0f;
                }
            }

            // La lista de juegos.
            const float listaY = y + 146.0f, fila = 72.0f;
            const int caben = std::max(1, (int)((y + height - 12.0f - listaY) / fila));
            if (this->elegido < this->primero) this->primero = this->elegido;
            if (this->elegido >= this->primero + caben) this->primero = this->elegido - caben + 1;
            if (vis.empty()) {
                nvgFontSize(vg, 18.0f);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                nvgFillColor(vg, tenue);
                nvgBeginPath(vg);
                nvgText(vg, izq + 10.0f, listaY + 10.0f, (js.empty() ? "menus/trucos/sin_juegos"_i18n : "menus/trucos/filtro_vacio"_i18n).c_str(), nullptr);
            }
            for (int k = 0; k < caben && this->primero + k < (int)vis.size(); ++k) {
                const int idx = this->primero + k;
                const Juego& j = js[vis[idx]];
                const float fy = listaY + k * fila;
                if (idx == this->elegido) {
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, izq, fy + 2, anchoIzq, fila - 4, 10.0f);
                    nvgFillColor(vg, this->a(ctx->theme->highlightBackgroundColor));
                    nvgFill(vg);
                    nvgStrokeColor(vg, this->a(ctx->theme->highlightColor1));
                    nvgStrokeWidth(vg, 2.0f);
                    nvgStroke(vg);
                }
                // Icono, creado la primera vez que se ve.
                auto it = this->iconos.find(j.tid);
                if (it == this->iconos.end())
                    it = this->iconos.emplace(j.tid, j.icono.empty() ? 0 : nvgCreateImageMem(vg, 0, const_cast<unsigned char*>(j.icono.data()), j.icono.size())).first;
                const float lado = 50.0f, ix = izq + 12.0f, iy = fy + (fila - lado) / 2;
                nvgBeginPath(vg);
                nvgRoundedRect(vg, ix, iy, lado, lado, 9.0f);
                if (it->second > 0)
                    nvgFillPaint(vg, nvgImagePattern(vg, ix, iy, lado, lado, 0, it->second, this->alpha));
                else
                    nvgFillColor(vg, this->a(nvgRGBA(255, 255, 255, 25)));
                nvgFill(vg);

                const std::string pill = etiquetaDe(j.estado);
                nvgFontSize(vg, 15.0f);
                const float wp = nvgTextBounds(vg, 0, 0, pill.c_str(), nullptr, nullptr) + 22.0f;
                pildora(vg, izq + anchoIzq - 14.0f - wp, fy + fila / 2, pill, coloresDe(j.estado), this->alpha);

                const float tx = ix + lado + 14.0f, tw = izq + anchoIzq - 28.0f - wp - tx;
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                nvgFontSize(vg, 19.0f);
                nvgFillColor(vg, texto);
                nvgBeginPath(vg);
                nvgText(vg, tx, fy + 13.0f, cortar(vg, j.nombre, tw).c_str(), nullptr);
                nvgFontSize(vg, 15.0f);
                nvgFillColor(vg, tenue);
                nvgBeginPath(vg);
                nvgText(vg, tx, fy + 40.0f, cortar(vg, fmt::format("menus/trucos/tienes"_i18n, nombreVersion(j, j.version)), tw).c_str(), nullptr);
            }

            // Separador y detalle del juego elegido.
            nvgBeginPath(vg);
            nvgRect(vg, der - 15.0f, y + 18.0f, 1.0f, height - 36.0f);
            nvgFillColor(vg, this->a(ctx->theme->sidebarSeparatorColor));
            nvgFill(vg);

            const Juego* j = this->actual();
            if (!j) return;
            float dy = y + 18.0f;
            {
                auto it = this->iconos.find(j->tid);
                const float lado = 72.0f;
                nvgBeginPath(vg);
                nvgRoundedRect(vg, der, dy, lado, lado, 12.0f);
                if (it != this->iconos.end() && it->second > 0)
                    nvgFillPaint(vg, nvgImagePattern(vg, der, dy, lado, lado, 0, it->second, this->alpha));
                else
                    nvgFillColor(vg, this->a(nvgRGBA(255, 255, 255, 25)));
                nvgFill(vg);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                nvgFontSize(vg, 21.0f);
                nvgFillColor(vg, texto);
                nvgBeginPath(vg);
                nvgText(vg, der + lado + 16.0f, dy + 8.0f, cortar(vg, j->nombre, anchoDer - lado - 16.0f).c_str(), nullptr);
                nvgFontSize(vg, 15.0f);
                nvgFillColor(vg, tenue);
                nvgBeginPath(vg);
                nvgText(vg, der + lado + 16.0f, dy + 40.0f, (nombreVersion(*j, j->version) + " · " + util::formatApplicationId(j->tid)).c_str(), nullptr);
                dy += lado + 18.0f;
            }

            // El mensaje: lo que importa, en una caja del color del estado.
            {
                std::string titulo, cuerpo;
                Estado e = j->estado;
                if (!this->lectura->baseBajada) {
                    e = SIN_TRUCOS;
                    titulo = "menus/trucos/d_sin_base"_i18n;
                    cuerpo = "menus/trucos/d_sin_base_sub"_i18n;
                }
                else if (e == CON_TRUCOS) {
                    titulo = "menus/trucos/d_ok"_i18n;
                    cuerpo = fmt::format("menus/trucos/d_ok_sub"_i18n, j->trucos);
                }
                else if (e == OTRA_VERSION) {
                    titulo = "menus/trucos/d_otra"_i18n;
                    cuerpo = fmt::format("menus/trucos/d_otra_sub"_i18n, nombreVersion(*j, j->version), conTrucosLista(*j));
                }
                else if (e == VERSION_DESCONOCIDA) {
                    titulo = "menus/trucos/d_desconocida"_i18n;
                    cuerpo = fmt::format("menus/trucos/d_desconocida_sub"_i18n, conTrucosLista(*j));
                }
                else {
                    titulo = "menus/trucos/d_sin"_i18n;
                    cuerpo = "menus/trucos/d_sin_sub"_i18n;
                }
                const Colores c = coloresDe(e);
                nvgFontSize(vg, 16.0f);
                nvgTextLineHeight(vg, 1.3f);
                float caja[4];
                nvgTextBoxBounds(vg, 0, 0, anchoDer - 36.0f, cuerpo.c_str(), nullptr, caja);
                const float alto = 52.0f + (caja[3] - caja[1]);
                nvgBeginPath(vg);
                nvgRoundedRect(vg, der, dy, anchoDer, alto, 12.0f);
                nvgFillColor(vg, this->a(c.fondo));
                nvgFill(vg);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                nvgFillColor(vg, this->a(c.texto));
                nvgFontSize(vg, 19.0f);
                nvgBeginPath(vg);
                nvgText(vg, der + 18.0f, dy + 14.0f, titulo.c_str(), nullptr);
                nvgFontSize(vg, 16.0f);
                nvgBeginPath(vg);
                nvgTextBox(vg, der + 18.0f, dy + 42.0f, anchoDer - 36.0f, cuerpo.c_str(), nullptr);
                dy += alto + 20.0f;
            }

            // Versiones que conoce la base.
            if (!j->versiones.empty()) {
                nvgFontSize(vg, 15.0f);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                nvgFillColor(vg, tenue);
                nvgBeginPath(vg);
                nvgText(vg, der, dy, "menus/trucos/versiones"_i18n.c_str(), nullptr);
                dy += 26.0f;
                const float alto = 38.0f;
                const int caben2 = std::max(1, (int)((y + height - 12.0f - dy) / alto));
                for (int k = 0; k < (int)j->versiones.size() && k < caben2; ++k) {
                    const Version& v = j->versiones[k];
                    const float vy = dy + k * alto;
                    if (k == caben2 - 1 && (int)j->versiones.size() > caben2) {
                        nvgFillColor(vg, tenue);
                        nvgBeginPath(vg);
                        nvgText(vg, der, vy + 8.0f, fmt::format("menus/trucos/y_mas"_i18n, j->versiones.size() - k).c_str(), nullptr);
                        break;
                    }
                    nvgFontSize(vg, 17.0f);
                    nvgFillColor(vg, texto);
                    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                    nvgBeginPath(vg);
                    nvgText(vg, der, vy + alto / 2, nombreVersion(*j, v.numero).c_str(), nullptr);
                    std::string nota;
                    if (v.numero == j->version) nota = "menus/trucos/la_tuya"_i18n;
                    else if (j->hayUltima && v.numero == j->ultima) nota = "menus/trucos/la_ultima"_i18n;
                    if (!nota.empty()) {
                        nvgFontSize(vg, 15.0f);
                        nvgFillColor(vg, tenue);
                        nvgBeginPath(vg);
                        nvgText(vg, der + anchoDer * 0.38f, vy + alto / 2, nota.c_str(), nullptr);
                    }
                    const std::string p = v.conTrucos ? "menus/trucos/pill_ok"_i18n : "menus/trucos/pill_sin"_i18n;
                    nvgFontSize(vg, 14.0f);
                    const float wp = nvgTextBounds(vg, 0, 0, p.c_str(), nullptr, nullptr) + 22.0f;
                    pildora(vg, der + anchoDer - wp, vy + alto / 2, p, coloresDe(v.conTrucos ? CON_TRUCOS : SIN_TRUCOS), this->alpha, 14.0f);
                    nvgBeginPath(vg);
                    nvgRect(vg, der, vy + alto - 1, anchoDer, 1);
                    nvgFillColor(vg, this->a(ctx->theme->listItemSeparatorColor));
                    nvgFill(vg);
                }
            }
        }

        void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override {}
    };

}  // namespace

void trucos::abrir()
{
    brls::AppletFrame* frame = new brls::AppletFrame(true, true);
    frame->setTitle("menus/trucos/titulo"_i18n);
    frame->setContentView(new Vista());
    brls::Application::pushView(frame);
}
