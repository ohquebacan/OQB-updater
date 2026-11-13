#include "nexus_tab.hpp"

#include <switch.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#include "confirm_page.hpp"
#include "constants.hpp"
#include "current_cfw.hpp"
#include "download.hpp"
#include "extract.hpp"
#include "dialogue_page.hpp"
#include "dialogo_centrado.hpp"
#include "forwarder.hpp"
#include "fs.hpp"
#include "nexus_eject.hpp"
#include "oqb_theme.hpp"
#include "pack_list_item.hpp"
#include "pack_version.hpp"
#include "pagina_oqb.hpp"
#include "progress_event.hpp"
#include "utils.hpp"
#include "worker_page.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {

    constexpr const char INI[] = "/atmosphere/config/system_settings.ini";
    constexpr const char REGISTRO[] = "/config/aio-switch-updater/nexus_bacan.json";
    constexpr const char RESPALDO[] = "/config/aio-switch-updater/nexus_respaldo/";
    constexpr const char ESTADO_USB[] = "/atmosphere/logs/usb_storage_status.txt";
    constexpr const char ESTADO_EXPULSAR[] = "/atmosphere/logs/usb_eject_status.txt";
    constexpr const char COMANDO_EXPULSAR[] = "/atmosphere/logs/usb_eject_command.bin";
    constexpr const char TRAZA_USB[] = "/atmosphere/logs/usb_loader_trace.txt";
    constexpr const char LOG_APP[] = "/config/nexusmanager/log.txt";
    constexpr const char TITLE_MAP[] = "/atmosphere/usb_content/title_map.bin";
    constexpr const char USB_BADGES[] = "/atmosphere/usb_badges";
    constexpr const char NRO[] = "/switch/nexusmanager/NexusManager.nro";
    constexpr const char OVL[] = "/switch/.overlays/nexus-bacan.ovl";
    constexpr const char ULTRAHAND[] = "/switch/.overlays/ovlmenu.ovl";
    constexpr const char FUSEE[] = "/bootloader/payloads/fusee.bin";
    constexpr const char SOPORTE[] = "/oqb/soporte/";

    // Lo que el pack de Nexus pisa y se respalda antes (y se restaura al volver).
    constexpr const char* PISADOS[] = {"/atmosphere/package3", "/atmosphere/stratosphere.romfs", FUSEE};

    bool existe(const std::string& ruta)
    {
        std::error_code ec;
        return std::filesystem::exists(ruta, ec);
    }

    std::string texto(const nlohmann::ordered_json& j, const char* clave)
    {
        const auto v = j.find(clave);
        return v != j.end() && v->is_string() ? v->get<std::string>() : "";
    }

    std::string leerTodo(const std::string& ruta, size_t ultimos = 0)
    {
        std::ifstream f(ruta, std::ios::binary);
        if (!f) return "";
        std::stringstream s;
        s << f.rdbuf();
        std::string t = s.str();
        if (ultimos && t.size() > ultimos) t = "[...]\n" + t.substr(t.size() - ultimos);
        return t;
    }

    std::string recortar(std::string s)
    {
        while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
        size_t i = 0;
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
        return s.substr(i);
    }

    // --- system_settings.ini --------------------------------------------------
    /* Nexus lee las dos lineas del USB SOLO de la seccion [atmosphere]: en otra
       seccion las ignora, no arranca su servicio y los juegos quedan en negro. */
    bool esLineaUsb(const std::string& linea)
    {
        const std::string t = recortar(linea);
        return t.rfind("enable_usb_content", 0) == 0;
    }

    bool iniTieneUsb()
    {
        std::ifstream f(INI);
        std::string linea, seccion;
        bool contenido = false, ruta = false;
        while (std::getline(f, linea)) {
            const std::string t = recortar(linea);
            if (!t.empty() && t[0] == '[') {
                seccion = t;
                continue;
            }
            if (seccion != "[atmosphere]" || !esLineaUsb(t)) continue;
            const auto igual = t.find('=');
            if (igual == std::string::npos) continue;
            const std::string clave = recortar(t.substr(0, igual)), valor = recortar(t.substr(igual + 1));
            const bool on = valor == "u8!0x1";
            if (clave == "enable_usb_content") contenido = on;
            else if (clave == "enable_usb_content_route") ruta = on;
        }
        return contenido && ruta;
    }

    bool ponerUsbEnIni(bool encender)
    {
        std::vector<std::string> lineas;
        {
            std::ifstream f(INI);
            std::string linea;
            while (std::getline(f, linea)) {
                if (!linea.empty() && linea.back() == '\r') linea.pop_back();
                if (!esLineaUsb(linea)) lineas.push_back(linea);
            }
        }
        if (encender) {
            const std::vector<std::string> usb = {"enable_usb_content = u8!0x1", "enable_usb_content_route = u8!0x1"};
            auto it = std::find_if(lineas.begin(), lineas.end(), [](const std::string& l) { return recortar(l) == "[atmosphere]"; });
            if (it == lineas.end()) {
                if (!lineas.empty() && !recortar(lineas.back()).empty()) lineas.push_back("");
                lineas.push_back("[atmosphere]");
                lineas.insert(lineas.end(), usb.begin(), usb.end());
            }
            else
                lineas.insert(it + 1, usb.begin(), usb.end());
        }
        fs::createTree("/atmosphere/config/");
        const std::string tmp = std::string(INI) + ".tmp";
        {
            std::ofstream f(tmp, std::ios::trunc);
            if (!f) return false;
            for (const auto& l : lineas) f << l << "\n";
            if (!f) return false;
        }
        std::error_code ec;
        std::filesystem::remove(INI, ec);
        std::filesystem::rename(tmp, INI, ec);
        return !ec;
    }

    // --- Estado del USB --------------------------------------------------------
    /* Nexus escribe el estado en la SD cada pocos segundos. Se lee de ahi y no
       del servicio: preguntarle con el USB a medio montar podia quedarse esperando. */
    struct Estado {
        bool archivo = false, activado = false, montado = false, listo = false;
        std::string formato;
        u64 libre = 0, total = 0;
        bool conectado() const { return montado && listo; }
    };

    Estado leerEstado()
    {
        Estado e;
        std::ifstream f(ESTADO_USB);
        if (!f) return e;
        e.archivo = true;
        std::string linea;
        while (std::getline(f, linea)) {
            const auto igual = linea.find('=');
            if (igual == std::string::npos) continue;
            const std::string k = recortar(linea.substr(0, igual)), v = recortar(linea.substr(igual + 1));
            const unsigned long long n = std::strtoull(v.c_str(), nullptr, 0);
            if (k == "enabled") e.activado = n != 0;
            else if (k == "mounted") e.montado = n != 0;
            else if (k == "ready") e.listo = n != 0;
            else if (k == "filesystem") e.formato = v;
            else if (k == "free_space") e.libre = n;
            else if (k == "total_space") e.total = n;
        }
        return e;
    }

    std::string gb(u64 bytes)
    {
        char b[32];
        std::snprintf(b, sizeof(b), "%.1f", double(bytes) / (1024.0 * 1024 * 1024));
        std::string s = b;
        std::replace(s.begin(), s.end(), '.', ',');
        return s;
    }

    /* Los juegos movidos al USB, de la tabla de rutas de Nexus (title_map.bin:
       cabecera de 16 bytes "USBT", version 1 y cantidad; luego 16 bytes por
       titulo). Juego, actualizacion y DLC cuentan como un solo juego. */
    std::vector<u64> juegosEnUsb()
    {
        std::set<u64> juegos;
        const std::string b = leerTodo(TITLE_MAP);
        auto u32At = [&b](size_t p) { u32 v; std::memcpy(&v, b.data() + p, 4); return v; };
        auto u64At = [&b](size_t p) { u64 v; std::memcpy(&v, b.data() + p, 8); return v; };
        if (b.size() >= 16 && u32At(0) == 0x54425355 && u32At(4) == 1) {
            const u32 n = u32At(8);
            if (b.size() == 16 + size_t(n) * 16)
                for (u32 i = 0; i < n; ++i) {
                    const u64 id = u64At(16 + size_t(i) * 16);
                    if (id) juegos.insert(id & ~0x1FFFULL);
                }
        }
        /* Los que el USB trae de otra consola no estan en la tabla de esta: Nexus
           los registra al conectarlo y les pone la marca del HOME. Esas marcas
           (/atmosphere/usb_badges/<id>-0.ubg) dicen que juegos ve en el USB. */
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(USB_BADGES, ec)) {
            const std::string n = e.path().filename().string();
            if (n.size() != 22 || n.compare(16, 6, "-0.ubg") != 0) continue;
            char* fin = nullptr;
            const u64 id = std::strtoull(n.substr(0, 16).c_str(), &fin, 16);
            if (id && fin && *fin == 0) juegos.insert(id & ~0x1FFFULL);
        }
        return {juegos.begin(), juegos.end()};
    }

    std::string nombreJuego(u64 id)
    {
        auto control = std::make_unique<NsApplicationControlData>();
        u64 size = 0;
        NacpLanguageEntry* entrada = nullptr;
        /* Primero la cache del sistema (no lee el juego, sirve aunque el USB no
           este); si no esta ahi, del contenido. */
        for (const auto fuente : {NsApplicationControlSource_CacheOnly, NsApplicationControlSource_Storage})
            if (R_SUCCEEDED(nsGetApplicationControlData(fuente, id, control.get(), sizeof(NsApplicationControlData), &size)) &&
                size >= sizeof(control->nacp) && R_SUCCEEDED(nacpGetLanguageEntry(&control->nacp, &entrada)) && entrada && entrada->name[0])
                return entrada->name;
        return fmt::format("{:016X}", id);
    }

    // --- Registro ----------------------------------------------------------------
    nlohmann::ordered_json registro()
    {
        const nlohmann::ordered_json j = fs::parseJsonFile(REGISTRO);
        return j.is_object() ? j : nlohmann::ordered_json::object();
    }

    std::string firmware()
    {
        SetSysFirmwareVersion ver;
        return R_SUCCEEDED(setsysGetFirmwareVersion(&ver)) ? std::string(ver.display_version) : "";
    }

    bool firmwareCompatible(const nlohmann::ordered_json& datos, const std::string& fw)
    {
        const auto lista = datos.find("firmwares");
        if (lista == datos.end() || !lista->is_array()) return false;
        for (const auto& f : *lista)
            if (f.is_string() && f.get<std::string>() == fw) return true;
        return false;
    }

    std::string claveDe(const std::string& url) { return packVersion::keyFromUrl(url); }

    /* El pack de overclock arranca con su propio hoc.kip y exosphere.bin, y con
       esa entrada los juegos del USB no abren (Celeste y un port, 13-nov-2026;
       desde una entrada sin esas dos piezas sí). Hasta revisarlo, Nexus solo se
       instala sobre el pack normal. Se mira la clave del pack y el arranque. */
    bool packConOverclock()
    {
        std::string clave = packVersion::current();
        std::transform(clave.begin(), clave.end(), clave.begin(), [](unsigned char c) { return std::tolower(c); });
        if (clave.find("-oc-") != std::string::npos) return true;
        std::string ini = leerTodo(HEKATE_IPL_PATH);
        std::transform(ini.begin(), ini.end(), ini.begin(), [](unsigned char c) { return std::tolower(c); });
        return ini.find("kips/hoc.kip") != std::string::npos;
    }

    // La clave del pack normal que corresponde al de overclock (OQB-HATS-PACK-OC-23.0.zip -> OQB-HATS-PACK-23.0.zip).
    std::string clavePackNormal()
    {
        std::string clave = packVersion::current();
        const auto p = clave.find("-OC-");
        if (p != std::string::npos) clave.erase(p, 3);
        return clave;
    }

    // --- Ventanas ----------------------------------------------------------------
    /* Un aviso con OK, centrado: el Dialog de borealis crece solo hacia abajo
       cuando el texto no entra y quedaba por debajo de la mitad. */
    void aviso(const std::string& texto, unsigned alto = 340)
    {
        brls::Dialog* d = new DialogoCentrado(texto, alto);
        d->addButton("menus/common/ok"_i18n, [d](brls::View*) { d->close(); });
        d->setCancelable(true);
        d->open();
    }

    /* Un parrafo que toma el foco sin dibujar el marco: la ventana abre arriba
       del texto (no en el boton de abajo, que la desplazaba y cortaba el
       principio) y se recorre con la cruceta. */
    class Parrafo : public brls::Label
    {
    public:
        using brls::Label::Label;
        brls::View* getDefaultFocus() override { return this; }
        void onFocusGained() override
        {
            this->focused = true;
            this->getFocusEvent()->fire(this);
            if (this->hasParent()) this->getParent()->onChildFocusGained(this);
        }
    };

    void abrirTexto(const std::string& titulo, const std::string& cuerpo)
    {
        brls::List* l = new brls::List();
        brls::Label* t = new Parrafo(brls::LabelStyle::REGULAR, cuerpo, true);
        t->setFontSize(brls::Application::getStyle()->Label.descriptionFontSize + 2);
        l->addView(t);
        brls::ListItem* volver = new brls::ListItem("menus/common/back"_i18n);
        volver->getClickEvent()->subscribe([](brls::View*) { brls::Application::popView(); });
        l->addView(volver);
        brls::AppletFrame* frame = new brls::AppletFrame(true, true);
        frame->setContentView(l);
        brls::PopupFrame::open(titulo, frame, "", "");
    }

    void abrirGuia(const nlohmann::ordered_json& datos)
    {
        std::string cuerpo;
        const auto pasos = datos.find("pasos");
        int n = 0;
        if (pasos != datos.end() && pasos->is_array())
            for (const auto& p : *pasos) {
                if (!p.is_object()) continue;
                cuerpo += (cuerpo.empty() ? "" : "\n\n") + std::to_string(++n) + ".  " + texto(p, "titulo") + "\n" + texto(p, "texto");
            }
        if (cuerpo.empty()) cuerpo = "menus/nexus/guia_texto"_i18n;
        abrirTexto("menus/nexus/guia"_i18n, cuerpo);
    }

    // El acceso directo de Nexus Bacán en el menú HOME (el mismo forwarder que las demás apps).
    Result crearAcceso()
    {
        fwd::Config config;
        Result rc = fwd::configFromNro(NRO, config);
        if (R_FAILED(rc)) return rc;
        fwd::aplicarIconoPropio(config);
        return fwd::install(config, [](int step, int total) {
            ProgressEvent::instance().setTotalSteps(total);
            ProgressEvent::instance().setStep(step);
        });
    }

    void instalar(const std::string& url, const std::string& fecha, bool erista, const std::string& zipLocal = "")
    {
        brls::StagedAppletFrame* frame = new brls::StagedAppletFrame();
        frame->setTitle("menus/nexus/instalar"_i18n);
        ConfirmPage* explica = new ConfirmPage(frame, "menus/nexus/instalar_explica"_i18n);
        explica->explicacion();
        frame->addStage(explica);
        frame->addStage(new WorkerPage(frame, "menus/common/downloading"_i18n, [url, zipLocal]() {
            if (!zipLocal.empty() && existe(zipLocal)) {
                fs::createTree(DOWNLOAD_PATH);
                fs::copyFile(zipLocal, CUSTOM_FILENAME);
                ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
            }
            else
                util::downloadArchive(url, contentType::custom);
        }));
        auto sinAcceso = std::make_shared<std::string>();  // la ruta del .nro si falta su acceso directo
        auto crear = std::make_shared<bool>(false);
        frame->addStage(new WorkerPage(frame, "menus/common/extracting"_i18n, [url, fecha, sinAcceso]() {
            if (!util::isArchive(CUSTOM_FILENAME)) {
                ProgressEvent::instance().setStatusCode(STATUS_INCOMPLETE_DOWNLOAD);
                ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
                return;
            }
            if (!extract::hayEspacioPara(CUSTOM_FILENAME)) {
                ProgressEvent::instance().setStatusCode(STATUS_NO_SPACE);
                ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
                return;
            }
            nlohmann::ordered_json reg = registro();
            /* El respaldo es del pack normal: si Nexus ya estaba (una version
               nueva encima, o puesto a mano), los archivos de ahora son de
               Nexus y no se respaldan. Sin respaldo, volver al pack normal lo
               reinstala desde Packs y firmwares. */
            const bool yaEstaba = iniTieneUsb();
            if (!yaEstaba) {
                fs::removeDir(RESPALDO);
                fs::createTree(std::string(RESPALDO) + "atmosphere/");
                fs::createTree(std::string(RESPALDO) + "bootloader/payloads/");
                nlohmann::ordered_json habia = nlohmann::ordered_json::object();
                for (const char* p : PISADOS) {
                    const bool hay = existe(p);
                    habia[p] = hay;
                    if (hay) fs::copyFile(p, std::string(RESPALDO) + (p + 1));
                }
                reg["respaldo"] = habia;
                reg["pack_base"] = packVersion::current();
            }
            const bool habiaFusee = existe(FUSEE);
            extract::extract(CUSTOM_FILENAME, ROOT_PATH, false);
            /* El zip trae fusee.bin para que coincida con el package3 nuevo,
               pero solo se deja si el pack normal lo tenia (si no, sobra). */
            if (!habiaFusee && !(reg["respaldo"].is_object() && reg["respaldo"].value(FUSEE, false)))
                std::filesystem::remove(FUSEE);
            if (!ponerUsbEnIni(true)) {
                ProgressEvent::instance().setStatusCode(STATUS_NO_SPACE);
                ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
                return;
            }
            reg["version"] = fecha;
            reg["clave"] = claveDe(url);
            fs::writeJsonToFile(reg, REGISTRO);
            std::filesystem::remove(CUSTOM_FILENAME);
            if (existe(NRO) && !fwd::exists(NRO)) *sinAcceso = NRO;
            ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
        }));
        // Si ya tiene acceso directo, la pregunta se saltea sola.
        frame->addStage(new DialoguePage_optional(frame, "menus/nexus/acceso_pregunta"_i18n, crear, sinAcceso));
        frame->addStage(new WorkerPage(frame, "menus/apps/forwarder_creating"_i18n, [crear, sinAcceso]() {
            if (*crear && !sinAcceso->empty()) {
                const Result rc = crearAcceso();
                if (R_FAILED(rc)) {
                    nlohmann::ordered_json reg = registro();
                    reg["acceso_error"] = fwd::resultToString(rc);
                    fs::writeJsonToFile(reg, REGISTRO);
                }
            }
            ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
        }));
        frame->addStage(new ConfirmPage_AmsUpdate(frame, "menus/nexus/instalado_reinicia"_i18n, erista));
        brls::Application::pushView(frame);
    }

    void volverAlNormal(bool erista, std::function<bool(const std::string&)> reinstalarPack)
    {
        const nlohmann::ordered_json reg = registro();
        const bool conRespaldo = reg.contains("respaldo") && reg["respaldo"].is_object() &&
                                 existe(std::string(RESPALDO) + "atmosphere/package3");
        if (!conRespaldo) {
            /* Sin respaldo (se borro a mano, o se instalo con otra version): se
               apaga el USB y se reinstala el pack, que trae su propio Atmosphère. */
            ponerUsbEnIni(false);
            std::filesystem::remove(REGISTRO);
            const std::string base = packVersion::current();
            if (reinstalarPack && !base.empty() && reinstalarPack(base)) return;
            aviso("menus/nexus/sin_respaldo"_i18n);
            return;
        }
        brls::StagedAppletFrame* frame = new brls::StagedAppletFrame();
        frame->setTitle("menus/nexus/volver"_i18n);
        frame->addStage(new ConfirmPage(frame, "menus/nexus/volver_explica"_i18n));
        frame->addStage(new WorkerPage(frame, "menus/nexus/volver_trabajando"_i18n, [reg]() {
            for (const char* p : PISADOS) {
                const std::string copia = std::string(RESPALDO) + (p + 1);
                if (existe(copia)) fs::copyFile(copia, p);
                else if (!reg["respaldo"].value(p, true)) std::filesystem::remove(p);
            }
            ponerUsbEnIni(false);
            /* La app y el overlay sin el Atmosphère de Nexus no sirven. Las
               rutas de los juegos (/atmosphere/usb_content) se quedan: al
               reinstalar Nexus, esos juegos vuelven a abrir desde el USB. */
            std::filesystem::remove(NRO);
            std::filesystem::remove(OVL);
            fs::removeDir(RESPALDO);
            std::filesystem::remove(REGISTRO);
            ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
        }));
        frame->addStage(new ConfirmPage_AmsUpdate(frame, "menus/nexus/volver_reinicia"_i18n, erista));
        brls::Application::pushView(frame);
    }

    // Con el pack de overclock: explica y ofrece instalar el pack normal (la misma instalacion de Packs).
    void ofrecerPackNormal(std::function<bool(const std::string&)> reinstalarPack)
    {
        brls::Dialog* d = new DialogoCentrado("menus/nexus/pack_oc_explica"_i18n, 380);
        const std::string normal = clavePackNormal();
        d->addButton("menus/nexus/pack_oc_instalar"_i18n, [d, reinstalarPack, normal](brls::View*) {
            d->close([reinstalarPack, normal] {
                if (!reinstalarPack || normal.empty() || !reinstalarPack(normal)) aviso("menus/nexus/pack_oc_manual"_i18n);
            });
        });
        d->addButton("menus/common/cancel"_i18n, [d](brls::View*) { d->close(); });
        d->setCancelable(true);
        d->open();
    }

    u64 ahoraMs() { return armTicksToNs(armGetSystemTick()) / 1000000ULL; }

    bool leerExpulsion(nexus_eject::Snapshot& s)
    {
        const std::string t = leerTodo(ESTADO_EXPULSAR);
        return !t.empty() && nexus_eject::Parse(t.data(), t.size(), s);
    }

    /* Nexus no expulsa con un juego o una app abierta, y OQB Updater es una app.
       Se pide la expulsion y se cierra la app: Nexus espera hasta 15 s a que se
       cierre, expulsa y avisa con su notificacion. */
    void expulsar()
    {
        if (!leerEstado().conectado()) {
            aviso("menus/nexus/expulsar_sin_usb"_i18n);
            return;
        }
        brls::Dialog* d = new DialogoCentrado("menus/nexus/expulsar_explica"_i18n, 380);
        d->addButton("menus/nexus/expulsar_cerrar"_i18n, [d](brls::View*) {
            nexus_eject::Snapshot s{};
            bool escrito = false;
            if (leerExpulsion(s)) {
                nexus_eject::Request r{};
                r.magic = nexus_eject::Magic;
                r.boot = s.boot;
                r.sample_ms = ahoraMs();
                r.generation = s.generation;
                r.nonce = (armGetSystemTick() << 1) | 1;
                r.action = nexus_eject::Eject;
                if (FILE* f = std::fopen(COMANDO_EXPULSAR, "wb")) {
                    escrito = std::fwrite(&r, 1, sizeof(r), f) == sizeof(r);
                    std::fclose(f);
                }
            }
            d->close([escrito] {
                if (escrito) brls::Application::quit();
                else aviso("menus/nexus/expulsar_sin_servicio"_i18n);
            });
        });
        d->addButton("menus/common/cancel"_i18n, [d](brls::View*) { d->close(); });
        d->setCancelable(true);
        d->open();
    }

    // Un .txt con todo lo que sirve para soporte (sin claves, tickets ni partidas).
    std::string diagnostico()
    {
        fs::createTree(SOPORTE);
        const std::time_t t = std::time(nullptr);
        std::tm local{};
        localtime_r(&t, &local);
        char nombre[64];
        std::strftime(nombre, sizeof(nombre), "nexus-%Y%m%d-%H%M.txt", &local);
        const std::string ruta = std::string(SOPORTE) + nombre;
        std::ofstream f(ruta, std::ios::trunc);
        if (!f) return "";
        const Estado e = leerEstado();
        const nlohmann::ordered_json reg = registro();
        f << "== Nexus Bacán · diagnóstico\n";
        f << "OQB Updater: " << APP_VERSION << "\n";
        f << "Firmware: " << firmware() << "\n";
        f << "Atmosphère: " << CurrentCfw::getAmsInfo() << "\n";
        f << "Consola: " << (util::isErista() ? "Erista" : "Mariko") << "\n";
        f << "Pack: " << packVersion::current() << " · " << packVersion::installed(packVersion::current()) << "\n";
        f << "Registro Nexus: " << reg.dump() << "\n";
        f << "USB en la configuracion: " << (iniTieneUsb() ? "si" : "no") << "\n";
        f << "App: " << (existe(NRO) ? "si" : "no") << " · Overlay: " << (existe(OVL) ? "si" : "no")
          << " · Ultrahand: " << (existe(ULTRAHAND) ? "si" : "no") << "\n";
        const auto juegos = juegosEnUsb();
        f << "Juegos en el USB (" << juegos.size() << "):\n";
        for (u64 id : juegos) f << "  " << fmt::format("{:016X}", id) << "  " << nombreJuego(id) << "\n";
        f << "\n== system_settings.ini (lineas del USB)\n";
        {
            std::ifstream ini(INI);
            std::string l, seccion;
            while (std::getline(ini, l)) {
                const std::string r = recortar(l);
                if (!r.empty() && r[0] == '[') seccion = r;
                if (esLineaUsb(r)) f << seccion << " " << r << "\n";
            }
        }
        f << "\n== " << ESTADO_USB << "\n" << (e.archivo ? leerTodo(ESTADO_USB) : "(no existe)\n");
        f << "\n== " << ESTADO_EXPULSAR << "\n" << leerTodo(ESTADO_EXPULSAR);
        f << "\n== " << TRAZA_USB << " (final)\n" << leerTodo(TRAZA_USB, 64 * 1024);
        f << "\n== " << LOG_APP << " (final)\n" << leerTodo(LOG_APP, 64 * 1024);
        f << "\n";
        return f ? ruta : "";
    }

    /* Arriba de la seccion con Nexus instalado: el USB, los juegos que tiene y
       la SD, en tres cajas. Se vuelve a leer el estado cada pocos segundos. */
    class Tablero : public brls::View
    {
        Estado e;
        size_t juegos = 0;
        std::string nombres;
        s64 sdLibre = -1;
        u64 leido = 0;
        void releer()
        {
            this->e = leerEstado();
            const auto ids = juegosEnUsb();
            this->juegos = ids.size();
            this->nombres.clear();
            for (size_t i = 0; i < ids.size() && i < 3; ++i) this->nombres += (i ? ", " : "") + nombreJuego(ids[i]);
            if (ids.size() > 3) this->nombres += "…";
            fs::getFreeStorageSD(this->sdLibre);
            this->leido = ahoraMs();
        }

    public:
        Tablero()
        {
            this->setHeight(132);
            this->releer();
        }
        void layout(NVGcontext*, brls::Style*, brls::FontStash*) override {}
        void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override
        {
            if (ahoraMs() - this->leido > 3000) {
                this->e = leerEstado();
                this->leido = ahoraMs();
            }
            const float hueco = 16.0f, w = (width - 2 * hueco) / 3.0f;
            struct Caja {
                std::string k, v, m;
                NVGcolor color;
                float barra;
            };
            const bool hayUsb = this->e.conectado();
            Caja cajas[3];
            cajas[0].k = "menus/nexus/caja_usb"_i18n;
            if (hayUsb) {
                cajas[0].v = fmt::format("menus/nexus/conectado"_i18n, this->e.formato.empty() ? "?" : this->e.formato);
                cajas[0].m = fmt::format("menus/nexus/libres_de"_i18n, gb(this->e.libre), gb(this->e.total));
                cajas[0].color = oqb::oscuro() ? nvgRGB(192, 168, 238) : nvgRGB(90, 60, 170);
                cajas[0].barra = this->e.total ? 1.0f - float(double(this->e.libre) / double(this->e.total)) : -1;
            }
            else {
                cajas[0].v = this->e.archivo ? "menus/nexus/sin_usb"_i18n : "menus/nexus/sin_servicio"_i18n;
                cajas[0].m = this->e.archivo ? "menus/nexus/sin_usb_sub"_i18n : "menus/nexus/sin_servicio_sub"_i18n;
                cajas[0].color = oqb::doradoTexto();
                cajas[0].barra = -1;
            }
            cajas[1] = {"menus/nexus/caja_juegos"_i18n, std::to_string(this->juegos),
                        this->juegos ? this->nombres : "menus/nexus/ninguno"_i18n, ctx->theme->textColor, -1};
            cajas[2] = {"menus/nexus/caja_sd"_i18n, this->sdLibre >= 0 ? fmt::format("menus/nexus/gb_libres"_i18n, gb(this->sdLibre)) : "?",
                        "menus/nexus/caja_sd_sub"_i18n, ctx->theme->textColor, -1};
            for (int i = 0; i < 3; ++i) {
                const float cx = x + i * (w + hueco);
                nvgBeginPath(vg);
                nvgRoundedRect(vg, cx, y, w, height, 14.0f);
                nvgFillColor(vg, this->a(oqb::velo(oqb::oscuro() ? 12 : 10)));
                nvgFill(vg);
                nvgStrokeColor(vg, this->a(ctx->theme->sidebarSeparatorColor));
                nvgStrokeWidth(vg, 1.0f);
                nvgStroke(vg);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                nvgFontFaceId(vg, ctx->fontStash->regular);
                nvgFontSize(vg, 14.0f);
                nvgFillColor(vg, this->a(ctx->theme->descriptionColor));
                nvgBeginPath(vg);
                nvgText(vg, cx + 18, y + 16, cajas[i].k.c_str(), nullptr);
                nvgFontSize(vg, 21.0f);
                nvgFillColor(vg, this->a(cajas[i].color));
                nvgBeginPath(vg);
                nvgText(vg, cx + 18, y + 40, cajas[i].v.c_str(), nullptr);
                nvgFontSize(vg, 15.0f);
                nvgTextLineHeight(vg, 1.25f);
                nvgFillColor(vg, this->a(ctx->theme->descriptionColor));
                nvgBeginPath(vg);
                nvgTextBox(vg, cx + 18, y + 72, w - 36, cajas[i].m.c_str(), nullptr);
                if (cajas[i].barra >= 0) {
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, cx + 18, y + height - 20, w - 36, 6, 3);
                    nvgFillColor(vg, this->a(oqb::velo(24)));
                    nvgFill(vg);
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, cx + 18, y + height - 20, (w - 36) * std::clamp(cajas[i].barra, 0.0f, 1.0f), 6, 3);
                    nvgFillColor(vg, this->a(cajas[i].color));
                    nvgFill(vg);
                }
            }
        }
    };

}  // namespace

void NexusTab::aplicarPrueba(nlohmann::ordered_json& nxlinks)
{
    constexpr const char PRUEBA[] = "/config/aio-switch-updater/nexus_bacan_prueba.json";
    if (!existe(PRUEBA)) return;
    const nlohmann::ordered_json j = fs::parseJsonFile(PRUEBA);
    if (!j.is_object()) return;
    if (!nxlinks.is_object()) nxlinks = nlohmann::ordered_json::object();
    nxlinks["nexus_bacan"] = j;
}

bool NexusTab::available(const nlohmann::ordered_json& nxlinks)
{
    return nxlinks.is_object() && nxlinks.contains("nexus_bacan") && nxlinks["nexus_bacan"].is_object() &&
           !texto(nxlinks["nexus_bacan"], "url").empty();
}

bool NexusTab::instalado()
{
    return iniTieneUsb() && (existe(REGISTRO) || existe(NRO));
}

bool NexusTab::necesitaAtencion(const nlohmann::ordered_json& nxlinks, const nlohmann::ordered_json& packVersions)
{
    if (!available(nxlinks) || !existe(REGISTRO)) return false;
    if (!iniTieneUsb()) return true;  // el pack normal lo quito al actualizarse
    const nlohmann::ordered_json reg = registro();
    const std::string publicada = packVersion::published(packVersions, claveDe(texto(nxlinks["nexus_bacan"], "url"))).date;
    return !publicada.empty() && texto(reg, "version") != publicada;
}

NexusTab::NexusTab(const nlohmann::ordered_json& nxlinks, const nlohmann::ordered_json& packVersions,
                   std::function<bool(const std::string&)> reinstalarPack)
    : brls::List(), nxlinks(nxlinks)
{
    const nlohmann::ordered_json& datos = this->nxlinks["nexus_bacan"];
    const std::string url = texto(datos, "url");
    const std::string zipLocal = texto(datos, "zip_local");
    const std::string publicada = packVersion::published(packVersions, claveDe(url)).date;
    const bool erista = util::isErista();
    const nlohmann::ordered_json reg = registro();
    const bool hayRegistro = existe(REGISTRO);
    const bool usbEnIni = iniTieneUsb();
    // Puesto a mano (sin registro de la app) tambien cuenta: el USB encendido y la app en la SD.
    const bool activo = usbEnIni && (hayRegistro || existe(NRO));
    const std::string fw = firmware();
    const bool fwOk = firmwareCompatible(datos, fw);
    const bool conAms = CurrentCfw::running_cfw == CFW::ams;

    if (!activo) {
        /* Sin el pack: que es, si la consola sirve y un solo boton. */
        this->addView(new HeaderOqb("menus/nexus/que_es"_i18n));
        const std::string presentacion = texto(datos, "presentacion");
        this->addView(new brls::Label(brls::LabelStyle::REGULAR, presentacion.empty() ? "menus/nexus/presentacion"_i18n : presentacion, true));

        if (hayRegistro && !usbEnIni)
            this->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/nexus/quitado_por_pack"_i18n, true));

        this->addView(new HeaderOqb("menus/nexus/tu_consola"_i18n));
        PackListItem* itemFw = new PackListItem(fmt::format("menus/nexus/tu_firmware"_i18n, fw.empty() ? "?" : fw), "",
                                                "menus/nexus/tu_firmware_sub"_i18n);
        if (fwOk) itemFw->setPositiveValue("menus/nexus/compatible"_i18n);
        else itemFw->setWarningValue("menus/nexus/todavia_no"_i18n);
        this->addView(itemFw);

        const std::string base = packVersion::current();
        std::string nombreBase = base.substr(0, base.rfind(".zip"));
        std::replace(nombreBase.begin(), nombreBase.end(), '-', ' ');
        PackListItem* itemPack = new PackListItem(fmt::format("menus/nexus/tu_pack"_i18n, nombreBase.empty() ? "menus/nexus/pack_desconocido"_i18n : nombreBase),
                                                  "", "menus/nexus/tu_pack_sub"_i18n);
        const bool overclock = packConOverclock();
        if (overclock) itemPack->setSubLabel("menus/nexus/pack_oc_sub"_i18n);
        if (conAms && !overclock) itemPack->setPositiveValue("menus/nexus/compatible"_i18n);
        else itemPack->setWarningValue("menus/nexus/todavia_no"_i18n);
        if (overclock) itemPack->getClickEvent()->subscribe([reinstalarPack](brls::View*) { ofrecerPackNormal(reinstalarPack); });
        this->addView(itemPack);

        PackListItem* itemDisco = new PackListItem("menus/nexus/disco"_i18n, "", "menus/nexus/disco_sub"_i18n);
        itemDisco->setValue("exFAT", false, false);
        this->addView(itemDisco);

        this->addView(new HeaderOqb("menus/nexus/instalar_titulo"_i18n));
        PackListItem* inst = new PackListItem(hayRegistro ? "menus/nexus/reinstalar"_i18n : "menus/nexus/instalar"_i18n, "",
                                              "menus/nexus/instalar_sub"_i18n);
        inst->setWarningValue("menus/nexus/en_pruebas"_i18n);
        inst->getClickEvent()->subscribe([url, publicada, erista, fwOk, conAms, zipLocal, overclock, reinstalarPack](brls::View*) {
            if (overclock) {
                ofrecerPackNormal(reinstalarPack);
                return;
            }
            if (!conAms) {
                aviso("menus/nexus/sin_ams"_i18n);
                return;
            }
            if (!fwOk) {
                aviso("menus/nexus/fw_no"_i18n);
                return;
            }
            if (!erista && !existe(MARIKO_PAYLOAD_PATH)) {
                aviso("menus/errors/mariko_payload_missing"_i18n);
                return;
            }
            instalar(url, publicada, erista, zipLocal);
        });
        this->addView(inst);
    }
    else {
        this->addView(new Tablero());

        // Instalado sobre el pack de overclock (a mano o de antes): avisar arriba de todo.
        if (packConOverclock()) {
            PackListItem* oc = new PackListItem("menus/nexus/pack_oc_titulo"_i18n, "", "menus/nexus/pack_oc_sub"_i18n);
            oc->setWarningValue("menus/nexus/todavia_no"_i18n);
            oc->getClickEvent()->subscribe([reinstalarPack](brls::View*) { ofrecerPackNormal(reinstalarPack); });
            this->addView(oc);
        }

        // Version nueva del pack de Nexus: arriba de todo.
        if (!publicada.empty() && texto(reg, "version") != publicada) {
            PackListItem* nueva = new PackListItem("menus/nexus/actualizar"_i18n, "", "menus/nexus/actualizar_sub"_i18n);
            nueva->setWarningValue("menus/apps/new_version"_i18n);
            nueva->getClickEvent()->subscribe([url, publicada, erista, zipLocal](brls::View*) { instalar(url, publicada, erista, zipLocal); });
            this->addView(nueva);
        }

        this->addView(new HeaderOqb("menus/nexus/herramientas"_i18n));
        PackListItem* abrir = new PackListItem("menus/nexus/abrir"_i18n, "", "menus/nexus/abrir_sub"_i18n);
        if (existe(NRO)) abrir->setPositiveValue("menus/nexus/instalada"_i18n);
        else abrir->setWarningValue("menus/nexus/falta"_i18n);
        abrir->getClickEvent()->subscribe([url, publicada, erista, zipLocal](brls::View*) {
            if (existe(NRO)) util::abrirAppInstalada(NRO, "Nexus Bacán");
            else instalar(url, publicada, erista, zipLocal);
        });
        this->addView(abrir);

        if (existe(NRO) && !fwd::exists(NRO)) {
            PackListItem* acceso = new PackListItem("menus/nexus/acceso"_i18n, "", "menus/nexus/acceso_sub"_i18n);
            acceso->setWarningValue("menus/nexus/falta"_i18n);
            acceso->getClickEvent()->subscribe([](brls::View*) {
                brls::StagedAppletFrame* frame = new brls::StagedAppletFrame();
                frame->setTitle("menus/nexus/acceso"_i18n);
                auto mensaje = std::make_shared<std::string>();
                frame->addStage(new WorkerPage(frame, "menus/apps/forwarder_creating"_i18n, [mensaje]() {
                    const Result rc = crearAcceso();
                    *mensaje = R_SUCCEEDED(rc) ? "menus/nexus/acceso_listo"_i18n : fmt::format("menus/apps/forwarder_error"_i18n, fwd::resultToString(rc));
                    ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
                }));
                frame->addStage(new ConfirmPage_Deferred(frame, mensaje));
                brls::Application::pushView(frame);
            });
            this->addView(acceso);
        }

        brls::ListItem* eject = new brls::ListItem("menus/nexus/expulsar"_i18n, "", "menus/nexus/expulsar_sub"_i18n);
        eject->getClickEvent()->subscribe([](brls::View*) { expulsar(); });
        this->addView(eject);

        PackListItem* ovl = new PackListItem("menus/nexus/overlay"_i18n, "", existe(ULTRAHAND) ? "menus/nexus/overlay_sub"_i18n : "menus/nexus/overlay_sin_ultrahand"_i18n);
        if (existe(OVL) && existe(ULTRAHAND)) ovl->setPositiveValue("menus/nexus/instalado"_i18n);
        else ovl->setWarningValue("menus/nexus/falta"_i18n);
        ovl->getClickEvent()->subscribe([](brls::View*) { abrirTexto("menus/nexus/overlay"_i18n, "menus/nexus/overlay_texto"_i18n); });
        this->addView(ovl);

        brls::ListItem* volver = new brls::ListItem("menus/nexus/volver"_i18n, "", "menus/nexus/volver_sub"_i18n);
        volver->getClickEvent()->subscribe([erista, reinstalarPack](brls::View*) { volverAlNormal(erista, reinstalarPack); });
        this->addView(volver);
    }

    this->addView(new HeaderOqb("menus/nexus/ayuda"_i18n));
    brls::ListItem* guia = new brls::ListItem("menus/nexus/guia"_i18n, "", "menus/nexus/guia_sub"_i18n);
    const nlohmann::ordered_json copia = datos;
    guia->getClickEvent()->subscribe([copia](brls::View*) { abrirGuia(copia); });
    this->addView(guia);

    brls::ListItem* diag = new brls::ListItem("menus/nexus/diagnostico"_i18n, "", "menus/nexus/diagnostico_sub"_i18n);
    diag->getClickEvent()->subscribe([](brls::View*) {
        const std::string ruta = diagnostico();
        if (ruta.empty()) aviso("menus/nexus/diagnostico_error"_i18n);
        else aviso(fmt::format("menus/nexus/diagnostico_ok"_i18n, ruta));
    });
    this->addView(diag);
}
