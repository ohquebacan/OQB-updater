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
#include <iterator>
#include <map>
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
#include "soporte.hpp"
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
    /* Las dos piezas propias del pack de overclock (sus entradas de Hekate las
       cargan con kip1= y secmon=). Nexus las cambia por las suyas con HOC + USB
       y se respaldan como lo demas. */
    constexpr const char HOC_KIP[] = "/atmosphere/kips/hoc.kip";
    constexpr const char EXOSPHERE[] = "/atmosphere/exosphere.bin";
    // En el zip, las versiones para el pack de overclock vienen aparte y solo se ponen si hace falta.
    constexpr const char ZIP_OC[] = "/nexus-oc/";
    // Lo escribe Nexus Bacán tras cada escaneo del USB (usb_resumen.cpp).
    constexpr const char RESUMEN_USB[] = "/config/nexusmanager/usb-juegos.json";
    constexpr const char* PISADOS[] = {"/atmosphere/package3", "/atmosphere/stratosphere.romfs", FUSEE, HOC_KIP, EXOSPHERE};

    bool existe(const std::string& ruta)
    {
        std::error_code ec;
        return std::filesystem::exists(ruta, ec);
    }

    /* El overlay de Horizon-OC guarda los ajustes del overclock dentro del
       hoc.kip, en su tabla «CUST». Al cambiar un hoc.kip por otro se pasan los
       ajustes del que habia, para no perder la configuracion del usuario. Solo
       si las dos tablas son de la misma version (revision y version del kip);
       si no, el nuevo queda con los valores de fabrica. */
    constexpr size_t TABLA_HOC = 0x68C8;  // sizeof(ams::ldr::hoc::CustomizeTable) en HOC 2.6.0 (rev 7)

    bool pasarAjustesHoc(const std::string& desde, const std::string& hacia)
    {
        auto leer = [](const std::string& ruta, std::string& out) {
            std::ifstream f(ruta, std::ios::binary);
            out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            return f.good() || f.eof();
        };
        std::string a, b;
        if (!leer(desde, a) || !leer(hacia, b)) return false;
        const size_t ia = a.find("CUST"), ib = b.find("CUST");
        if (ia == std::string::npos || ib == std::string::npos) return false;
        if (ia + TABLA_HOC > a.size() || ib + TABLA_HOC > b.size()) return false;
        if (a.compare(ia, 12, b, ib, 12) != 0) return false;  // CUST + revision + version del kip
        if (a.compare(ia, TABLA_HOC, b, ib, TABLA_HOC) == 0) return true;   // ya iguales
        b.replace(ib, TABLA_HOC, a, ia, TABLA_HOC);
        const std::string tmp = hacia + ".tmp";
        {
            std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
            if (!o.write(b.data(), b.size())) return false;
        }
        std::error_code ec;
        std::filesystem::rename(tmp, hacia, ec);
        return !ec;
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
       los de HOC solos los juegos del USB no abren (Celeste y un port,
       13-nov-2026): hoc.kip reemplaza al loader y el de HOC no tiene el USB. Un
       zip de Nexus con "oc": true en el catalogo trae los dos con HOC 2.6.0 +
       USB, y se ponen en vez de los del pack. Se mira la clave del pack y el arranque. */
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
        rc = fwd::install(config, [](int step, int total) {
            ProgressEvent::instance().setTotalSteps(total);
            ProgressEvent::instance().setStep(step);
        });
        /* El HOME guarda el ícono de cada título en caché: al rehacer el acceso
           con el mismo ID seguía mostrando el anterior. Se le pide que vuelva a
           leerlos todos (InvalidateAllApplicationControlCache, ns:am cmd 405). */
        if (R_SUCCEEDED(rc)) {
            Service am{};
            if (R_SUCCEEDED(nsGetApplicationManagerInterface(&am))) {
                (void)serviceDispatch(&am, 405);
                serviceClose(&am);
            }
        }
        return rc;
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
                fs::createTree(std::string(RESPALDO) + "atmosphere/kips/");
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
            /* Pack de overclock: el hoc.kip y el exosphere.bin de Nexus (HOC 2.6.0
               + USB) reemplazan a los del pack. Con el pack normal no se usan. */
            const std::string kipOc = std::string(ZIP_OC) + "atmosphere/kips/hoc.kip";
            const std::string exoOc = std::string(ZIP_OC) + "atmosphere/exosphere.bin";
            reg["oc"] = false;
            if (packConOverclock() && existe(kipOc) && existe(exoOc)) {
                fs::createTree("/atmosphere/kips/");
                if (existe(HOC_KIP)) reg["oc_ajustes"] = pasarAjustesHoc(HOC_KIP, kipOc);
                fs::copyFile(kipOc, HOC_KIP);
                fs::copyFile(exoOc, EXOSPHERE);
                reg["oc"] = true;
            }
            fs::removeDir(ZIP_OC);
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
            /* Si ya tenía acceso, se rehace: el ícono y el nombre se copian al
               crearlo, así que con una versión nueva de la app el acceso viejo
               seguía mostrando el ícono anterior. */
            else if (existe(NRO) && fwd::exists(NRO)) {
                const Result r = crearAcceso();
                if (R_FAILED(r)) { nlohmann::ordered_json reg2 = registro(); reg2["acceso_error"] = fwd::resultToString(r); fs::writeJsonToFile(reg2, REGISTRO); }
            }
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
                // El overclock que se ajusto con Nexus puesto sigue igual con el hoc.kip original.
                if (std::string(p) == HOC_KIP && existe(copia) && existe(p)) pasarAjustesHoc(p, copia);
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
    /* Para los casos de «tras el reposo no abre nada» o «se queda en el logo»:
       la SD (tamaño real, espacio, CID para ver el fabricante), lo que corre al
       arrancar, y los errores que Atmosphère guardó. Nada de esto lleva claves. */
    void diagnosticoSd(std::ofstream& f)
    {
        f << "\n== SD\n";
        if (FsFileSystem* sd = fsdevGetDeviceFileSystem("sdmc")) {
            s64 libre = 0, total = 0;
            fsFsGetFreeSpace(sd, "/", &libre);
            fsFsGetTotalSpace(sd, "/", &total);
            f << "Espacio: " << fmt::format("{:.1f} GB libres de {:.1f} GB", libre / 1e9, total / 1e9) << "\n";
        }
        FsDeviceOperator op;
        if (R_SUCCEEDED(fsOpenDeviceOperator(&op))) {
            bool puesta = false;
            s64 velocidad = -1, area = -1;
            u8 cid[0x10] = {};
            fsDeviceOperatorIsSdCardInserted(&op, &puesta);
            fsDeviceOperatorGetSdCardSpeedMode(&op, &velocidad);
            fsDeviceOperatorGetSdCardUserAreaSize(&op, &area);
            const Result rc = fsDeviceOperatorGetSdCardCid(&op, cid, sizeof(cid), sizeof(cid));
            fsDeviceOperatorClose(&op);
            f << "Insertada: " << (puesta ? "si" : "no") << " · Modo de velocidad: " << velocidad << "\n";
            f << "Tamano de la tarjeta (segun la tarjeta): " << (area >= 0 ? fmt::format("{:.1f} GB", area / 1e9) : "?") << "\n";
            f << "CID: ";
            if (R_SUCCEEDED(rc)) for (u8 b : cid) f << fmt::format("{:02X}", b);
            else f << fmt::format("no se pudo leer (0x{:X})", rc);
            f << "\n";
        }
    }

    // Módulos de /atmosphere/contents: los que traen exefs.nsp corren solos y
    // son los que pueden chocar con Nexus. El resto son mods de juegos.
    void diagnosticoModulos(std::ofstream& f)
    {
        f << "\n== Modulos en /atmosphere/contents\n";
        std::error_code ec;
        size_t otros = 0;
        std::vector<std::string> lineas;
        for (const auto& d : std::filesystem::directory_iterator("/atmosphere/contents", ec)) {
            if (!d.is_directory(ec)) continue;
            const auto p = d.path();
            if (!existe((p / "exefs.nsp").string())) { ++otros; continue; }
            std::string nombre;
            try {
                std::ifstream tj((p / "toolbox.json").string());
                if (tj) nombre = texto(nlohmann::ordered_json::parse(tj), "name");
            } catch (...) {}
            lineas.push_back(p.filename().string() + (existe((p / "flags/boot2.flag").string()) ? "  arranca solo" : "  sin boot2") +
                             (nombre.empty() ? "" : "  " + nombre));
        }
        std::sort(lineas.begin(), lineas.end());
        for (const auto& l : lineas) f << "  " << l << "\n";
        f << "Otras carpetas (mods de juegos): " << otros << "\n";
        f << "Archivos en /atmosphere:\n";
        for (const auto& d : std::filesystem::directory_iterator("/atmosphere", ec))
            if (d.is_regular_file(ec)) f << "  " << d.path().filename().string() << "  " << d.file_size(ec) << "\n";
    }

    // Los últimos errores que guardó Atmosphère (los nombres llevan la fecha) y el texto del más reciente.
    void diagnosticoErrores(std::ofstream& f)
    {
        for (const char* carpeta : {"/atmosphere/crash_reports", "/atmosphere/fatal_reports"}) {
            std::error_code ec;
            std::vector<std::string> nombres;
            for (const auto& d : std::filesystem::directory_iterator(carpeta, ec))
                if (d.is_regular_file(ec)) nombres.push_back(d.path().filename().string());
            std::sort(nombres.begin(), nombres.end());
            f << "\n== " << carpeta << " (" << nombres.size() << ", ultimos 10)\n";
            for (size_t i = nombres.size() > 10 ? nombres.size() - 10 : 0; i < nombres.size(); ++i) f << "  " << nombres[i] << "\n";
            if (!nombres.empty() && nombres.back().size() > 4 && nombres.back().substr(nombres.back().size() - 4) == ".log") {
                std::string t = leerTodo(std::string(carpeta) + "/" + nombres.back());
                if (t.size() > 3000) t = t.substr(0, 3000) + "\n[...]";
                f << "-- " << nombres.back() << "\n" << t << "\n";
            }
        }
    }

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
        diagnosticoSd(f);
        diagnosticoModulos(f);
        f << "\n== " << HEKATE_IPL_PATH << "\n" << leerTodo(HEKATE_IPL_PATH, 16 * 1024);
        diagnosticoErrores(f);
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
    // --- Vistas propias del rediseno (maqueta 14-nov-2026) -------------------------
    NVGcolor violeta() { return oqb::oscuro() ? nvgRGB(192, 168, 238) : nvgRGB(90, 60, 170); }

    void textoArriba(NVGcontext* vg, float x, float y, float tam, NVGcolor c, const std::string& t)
    {
        nvgFontSize(vg, tam);
        nvgFillColor(vg, c);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgBeginPath(vg);
        nvgText(vg, x, y, t.c_str(), nullptr);
    }

    // Un parrafo partido en lineas; devuelve cuantas uso.
    int lineasDe(NVGcontext* vg, float x, float y, float ancho, float alto, int maximo, const std::string& t)
    {
        const char* p = t.c_str();
        const char* fin = p + t.size();
        NVGtextRow fila[1];
        int i = 0;
        for (; i < maximo && p < fin; ++i) {
            if (nvgTextBreakLines(vg, p, fin, ancho, fila, 1) <= 0) break;
            nvgBeginPath(vg);
            nvgText(vg, x, y + i * alto, fila[0].start, fila[0].end);
            p = fila[0].next;
        }
        return i;
    }

    void caja(NVGcontext* vg, float x, float y, float w, float h, NVGcolor relleno, NVGcolor borde)
    {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, w, h, 14.0f);
        nvgFillColor(vg, relleno);
        nvgFill(vg);
        nvgStrokeColor(vg, borde);
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);
    }

    int fuenteUpheaval(NVGcontext* vg)
    {
        static int f = -2;
        if (f == -2) {
            f = nvgFindFont(vg, "upheaval");
            if (f < 0) f = nvgCreateFont(vg, "upheaval", "romfs:/fonts/upheavtt.ttf");
        }
        return f;
    }

    /* Un aviso como franja, con su boton a la derecha: PaginaOqb dibuja ahi la
       entrada que la sigue (Actualizar, Reactivar...). Dorado si algo falta,
       violeta si es una novedad. */
    class Franja : public brls::View, public ConBoton
    {
        std::string fuerte, resto;
        bool dorado;

    public:
        Franja(std::string fuerte, std::string resto, bool dorado) : fuerte(std::move(fuerte)), resto(std::move(resto)), dorado(dorado) { this->setHeight(86); }
        void layout(NVGcontext*, brls::Style*, brls::FontStash*) override {}
        void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style*, brls::FrameContext* ctx) override
        {
            NVGcolor c = this->dorado ? oqb::doradoTexto() : violeta();
            NVGcolor fondo = c, borde = c;
            fondo.a = 0.10f;
            borde.a = 0.45f;
            caja(vg, x, y, width, height, this->a(fondo), this->a(borde));
            nvgFontFaceId(vg, ctx->fontStash->regular);
            const float tw = width - 40.0f - this->reservaDerecha;
            nvgFontSize(vg, 18.0f);
            nvgFillColor(vg, this->a(ctx->theme->textColor));
            const int n = lineasDe(vg, x + 20, y + 16, tw, 23.0f, 1, this->fuerte);
            nvgFontSize(vg, 15.0f);
            nvgFillColor(vg, this->a(ctx->theme->descriptionColor));
            lineasDe(vg, x + 20, y + 16 + n * 24.0f, tw, 19.0f, 2, this->resto);
        }
    };

    /* Sin Nexus: que hace, en dos lineas, con el boton Instalar a la derecha (la
       entrada que la sigue) y debajo del boton cuanto pesa y que se respalda. */
    class Presentacion : public brls::View, public ConBoton
    {
        std::string nota;

    public:
        explicit Presentacion(std::string nota) : nota(std::move(nota)) { this->setHeight(128); }
        void layout(NVGcontext*, brls::Style*, brls::FontStash*) override {}
        void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style*, brls::FrameContext* ctx) override
        {
            caja(vg, x, y, width, height, this->a(oqb::velo(oqb::oscuro() ? 12 : 10)), this->a(ctx->theme->sidebarSeparatorColor));
            nvgFontFaceId(vg, ctx->fontStash->regular);
            const float tw = width - 48.0f - this->reservaDerecha;
            nvgFontSize(vg, 20.0f);
            nvgFillColor(vg, this->a(ctx->theme->textColor));
            const int n = lineasDe(vg, x + 22, y + 20, tw, 26.0f, 2, "menus/nexus/presentacion_titulo"_i18n);
            nvgFontSize(vg, 15.0f);
            nvgFillColor(vg, this->a(ctx->theme->descriptionColor));
            lineasDe(vg, x + 22, y + 26 + n * 26.0f, tw, 20.0f, 3, "menus/nexus/presentacion_texto"_i18n);
            if (this->reservaDerecha > 0) {
                // Debajo del boton (200 x 50, centrado en alto y a 26 del borde).
                nvgFontSize(vg, 13.0f);
                nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
                nvgBeginPath(vg);
                nvgText(vg, x + width - 26.0f - 100.0f, y + (height + 50.0f) / 2 + 8.0f, this->nota.c_str(), nullptr);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            }
        }
    };

    /* Arriba de todo con Nexus puesto: tu USB en grande (estado, espacio y que
       juegos tiene) y al lado la SD. Abrir y Expulsar van justo debajo. */
    /* Lo que Nexus vio en el USB en su ultimo escaneo, tambien los juegos de otra
       consola (/config/nexusmanager/usb-juegos.json, lo escribe Nexus). Se junta
       con los de esta consola: id -> {nombre, estado}. Estado: "aqui" (esta en
       esta consola), "agregar" (de otra consola, listo para agregar) o "revisar". */
    struct JuegoUsb {
        std::string nombre, estado;
    };
    std::map<u64, JuegoUsb> juegosDelUsb()
    {
        std::map<u64, JuegoUsb> out;
        for (u64 id : juegosEnUsb()) out[id] = {"", "aqui"};
        const nlohmann::ordered_json j = fs::parseJsonFile(RESUMEN_USB);
        if (j.is_object() && j.contains("juegos") && j["juegos"].is_array())
            for (const auto& g : j["juegos"]) {
                if (!g.is_object()) continue;
                const u64 id = std::strtoull(g.value("id", std::string()).c_str(), nullptr, 16);
                if (!id) continue;
                auto& x = out[id];
                if (x.estado != "aqui") x.estado = g.value("estado", std::string("revisar"));
                if (x.nombre.empty()) x.nombre = g.value("nombre", std::string());
            }
        return out;
    }

    class TarjetaUsb : public brls::View
    {
        Estado e;
        std::vector<u64> ids;
        size_t porAgregar = 0;
        std::string nombres;
        s64 sdLibre = -1, sdTotal = -1;
        u64 leido = 0;

    public:
        TarjetaUsb()
        {
            this->setHeight(176);
            this->e = leerEstado();
            size_t mostrados = 0;
            for (const auto& [id, j] : juegosDelUsb()) {
                if (j.estado == "revisar") continue;
                this->ids.push_back(id);
                if (j.estado == "agregar") ++this->porAgregar;
                if (mostrados < 3) {
                    // Los de esta consola, con el nombre del sistema; los de otra, con el que vio Nexus.
                    const std::string n = j.estado == "aqui" ? nombreJuego(id) : j.nombre;
                    if (!n.empty()) this->nombres += (mostrados++ ? ", " : "") + n;
                }
            }
            if (this->ids.size() > mostrados && mostrados) this->nombres += "…";
            if (FsFileSystem* sd = fsdevGetDeviceFileSystem("sdmc")) {
                fsFsGetFreeSpace(sd, "/", &this->sdLibre);
                fsFsGetTotalSpace(sd, "/", &this->sdTotal);
            }
            this->leido = ahoraMs();
        }
        void layout(NVGcontext*, brls::Style*, brls::FontStash*) override {}
        void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style*, brls::FrameContext* ctx) override
        {
            if (ahoraMs() - this->leido > 3000) {
                this->e = leerEstado();
                this->leido = ahoraMs();
            }
            const NVGcolor texto = this->a(ctx->theme->textColor), tenue = this->a(ctx->theme->descriptionColor);
            const NVGcolor fondo = this->a(oqb::velo(oqb::oscuro() ? 12 : 10)), borde = this->a(ctx->theme->sidebarSeparatorColor);
            const int up = fuenteUpheaval(vg), regular = ctx->fontStash->regular;
            const float hueco = 16.0f, wu = (width - hueco) * 0.62f, ws = width - hueco - wu;

            // Tu USB.
            caja(vg, x, y, wu, height, fondo, borde);
            nvgFontFaceId(vg, regular);
            const bool hay = this->e.conectado();
            const std::string formato = this->e.formato.empty() ? "?" : this->e.formato;
            std::string cab = hay ? fmt::format("menus/nexus/usb_conectado"_i18n, formato) : "menus/nexus/usb_titulo"_i18n;
            std::transform(cab.begin(), cab.end(), cab.begin(), [](unsigned char c) { return c < 0x80 ? std::toupper(c) : c; });
            textoArriba(vg, x + 22, y + 18, 13.0f, tenue, cab);
            if (hay) {
                nvgFontFaceId(vg, up >= 0 ? up : regular);
                nvgFontSize(vg, 40.0f);
                nvgFillColor(vg, texto);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                const std::string libre = gb(this->e.libre) + " GB";
                nvgBeginPath(vg);
                const float fin = nvgText(vg, x + 22, y + 40, libre.c_str(), nullptr);
                nvgFontFaceId(vg, regular);
                textoArriba(vg, fin + 10, y + 56, 15.0f, tenue, fmt::format("menus/nexus/usb_libres_de"_i18n, gb(this->e.total)));
                const float bx = x + 22, by = y + 94, bw = wu - 44;
                nvgBeginPath(vg);
                nvgRoundedRect(vg, bx, by, bw, 10, 5);
                nvgFillColor(vg, this->a(oqb::velo(24)));
                nvgFill(vg);
                const float uso = this->e.total ? 1.0f - float(double(this->e.libre) / double(this->e.total)) : 0.0f;
                nvgBeginPath(vg);
                nvgRoundedRect(vg, bx, by, std::max(10.0f, bw * std::clamp(uso, 0.0f, 1.0f)), 10, 5);
                nvgFillColor(vg, this->a(violeta()));
                nvgFill(vg);
                std::string juegos = this->ids.empty() ? "menus/nexus/usb_sin_juegos"_i18n
                                     : this->ids.size() == 1 ? fmt::format("menus/nexus/usb_un_juego"_i18n, this->nombres)
                                                             : fmt::format("menus/nexus/usb_juegos"_i18n, this->ids.size(), this->nombres);
                // Los que vienen de otra consola y aqui todavia no se agregaron.
                if (this->porAgregar)
                    juegos += fmt::format(this->porAgregar == 1 ? "menus/nexus/usb_otra_uno"_i18n : "menus/nexus/usb_otra"_i18n, this->porAgregar);
                nvgFontSize(vg, 15.0f);
                nvgFillColor(vg, tenue);
                lineasDe(vg, x + 22, y + 120, wu - 44, 20.0f, 2, juegos);
            }
            else {
                nvgFontFaceId(vg, up >= 0 ? up : regular);
                textoArriba(vg, x + 22, y + 40, 40.0f, this->a(oqb::doradoTexto()),
                            this->e.archivo ? "menus/nexus/sin_usb"_i18n : "menus/nexus/sin_servicio"_i18n);
                nvgFontFaceId(vg, regular);
                const std::string sub = !this->e.archivo ? "menus/nexus/sin_servicio_sub"_i18n
                                        : this->ids.empty() ? "menus/nexus/usb_conectalo_sin"_i18n
                                                            : fmt::format("menus/nexus/usb_conectalo"_i18n, this->ids.size());
                nvgFontSize(vg, 15.0f);
                nvgFillColor(vg, tenue);
                lineasDe(vg, x + 22, y + 96, wu - 44, 20.0f, 3, sub);
            }

            // La SD.
            const float sx = x + wu + hueco;
            caja(vg, sx, y, ws, height, fondo, borde);
            nvgFontFaceId(vg, regular);
            textoArriba(vg, sx + 22, y + 18, 13.0f, tenue, "menus/nexus/sd_titulo"_i18n);
            const bool llena = this->sdTotal > 0 && this->sdLibre >= 0 && double(this->sdLibre) / double(this->sdTotal) < 0.15;
            nvgFontFaceId(vg, up >= 0 ? up : regular);
            textoArriba(vg, sx + 22, y + 40, 40.0f, llena ? this->a(oqb::doradoTexto()) : texto,
                        this->sdLibre >= 0 ? gb(this->sdLibre) + " GB" : "?");
            nvgFontFaceId(vg, regular);
            nvgFontSize(vg, 15.0f);
            nvgFillColor(vg, tenue);
            const std::string totalSd = this->sdTotal > 0 ? gb(this->sdTotal) : "?";
            int n = lineasDe(vg, sx + 22, y + 90, ws - 44, 20.0f, 2,
                             fmt::format(llena ? "menus/nexus/sd_casi_llena"_i18n : "menus/nexus/usb_libres_de"_i18n, totalSd));
            if (llena) lineasDe(vg, sx + 22, y + 96 + n * 20.0f, ws - 44, 20.0f, 3, "menus/nexus/sd_sugerencia"_i18n);
        }
    };

    // Lo instalado, en una fila de pildoras: verde si esta, dorado si falta.
    class FilaEstado : public brls::View
    {
        std::vector<std::pair<std::string, bool>> items;

    public:
        explicit FilaEstado(std::vector<std::pair<std::string, bool>> items) : items(std::move(items)) { this->setHeight(40); }
        void layout(NVGcontext*, brls::Style*, brls::FontStash*) override {}
        void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style*, brls::FrameContext* ctx) override
        {
            nvgFontFaceId(vg, ctx->fontStash->regular);
            nvgFontSize(vg, 15.0f);
            float px = x;
            for (const auto& [t, ok] : this->items) {
                const std::string s = std::string(ok ? "✓  " : "!  ") + t;
                const float w = nvgTextBounds(vg, 0, 0, s.c_str(), nullptr, nullptr) + 30.0f;
                if (px + w > x + width) break;
                NVGcolor c = ok ? oqb::verdeTexto() : oqb::doradoTexto();
                NVGcolor f = c;
                f.a = 0.10f;
                NVGcolor b = c;
                b.a = ok ? 0.25f : 0.55f;
                nvgBeginPath(vg);
                nvgRoundedRect(vg, px, y + 2, w, height - 4, (height - 4) / 2);
                nvgFillColor(vg, this->a(f));
                nvgFill(vg);
                nvgStrokeColor(vg, this->a(b));
                nvgStrokeWidth(vg, 1.0f);
                nvgStroke(vg);
                nvgFillColor(vg, this->a(ok ? ctx->theme->textColor : c));
                nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                nvgBeginPath(vg);
                nvgText(vg, px + w / 2, y + height / 2 + 1, s.c_str(), nullptr);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                px += w + 10.0f;
            }
        }
    };

    /* Quitar: antes de volver al pack, decir que juegos se quedan en el USB (sin
       Nexus no abren) y ofrecer abrir Nexus para traerlos a la SD. */
    u64 tamanoJuego(u64 id)
    {
        /* ApplicationOccupiedSize (switchbrew): 4 entradas de 0x20 bytes, cada una
           { u8 storage; pad[7]; u64 app; u64 patch; u64 dlc }. Si algo no cuadra, 0. */
        NsApplicationOccupiedSize o{};
        if (R_FAILED(nsCalculateApplicationOccupiedSize(id, &o))) return 0;
        u64 total = 0;
        for (int i = 0; i < 4; ++i)
            for (int k = 0; k < 3; ++k) {
                u64 v;
                std::memcpy(&v, o.unk_x0 + i * 0x20 + 8 + k * 8, 8);
                if (v > (1ULL << 40)) return 0;
                total += v;
            }
        return total;
    }

    void quitarNexus(bool erista, std::function<bool(const std::string&)> reinstalarPack)
    {
        const auto ids = juegosEnUsb();
        if (ids.empty()) {
            volverAlNormal(erista, reinstalarPack);
            return;
        }
        std::string lista;
        u64 total = 0;
        for (size_t i = 0; i < ids.size() && i < 6; ++i) {
            const u64 t = tamanoJuego(ids[i]);
            total += t;
            lista += "•  " + nombreJuego(ids[i]) + (t ? "  (" + gb(t) + " GB)" : "") + "\n";
        }
        if (ids.size() > 6) lista += "…\n";
        s64 libre = -1;
        fs::getFreeStorageSD(libre);
        const std::string sd = libre >= 0 ? fmt::format(total && u64(libre) > total ? "menus/nexus/quitar_caben"_i18n : "menus/nexus/quitar_no_caben"_i18n, gb(libre)) : "";
        brls::Dialog* d = new DialogoCentrado(fmt::format("menus/nexus/quitar_dialogo"_i18n, lista, sd), 520);
        d->addButton("menus/nexus/quitar_traer"_i18n, [d](brls::View*) { d->close([] { util::abrirAppInstalada(NRO, "Nexus Bacán"); }); });
        d->addButton("menus/nexus/quitar_igual"_i18n, [d, erista, reinstalarPack](brls::View*) { d->close([erista, reinstalarPack] { volverAlNormal(erista, reinstalarPack); }); });
        d->addButton("menus/common/cancel"_i18n, [d](brls::View*) { d->close(); });
        d->setCancelable(true);
        d->open();
    }

    // El catalogo puede decir con que packs se probo Nexus ("packs": [...]); sin esa lista, cualquiera.
    bool packProbado(const nlohmann::ordered_json& datos, const std::string& clave)
    {
        const auto lista = datos.find("packs");
        if (lista == datos.end() || !lista->is_array()) return true;
        for (const auto& p : *lista)
            if (p.is_string() && p.get<std::string>() == clave) return true;
        return false;
    }

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
    // Sobre el pack de overclock sin la version con HOC, y ya hay una publicada.
    if (packConOverclock() && !reg.value("oc", false) && nxlinks["nexus_bacan"].value("oc", false)) return true;
    return !publicada.empty() && texto(reg, "version") != publicada;
}

NexusTab::NexusTab(const nlohmann::ordered_json& nxlinks, const nlohmann::ordered_json& packVersions,
                   std::function<bool(const std::string&)> reinstalarPack)
    : brls::List(), nxlinks(nxlinks)
{
    const nlohmann::ordered_json& datos = this->nxlinks["nexus_bacan"];
    const std::string url = texto(datos, "url");
    /* Solo en el modo de prueba (nexus_bacan_prueba.json en la SD): un zip
       local en vez del publicado. El catalogo publicado nunca trae esta clave. */
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
    // El zip publicado trae la version para el pack de overclock.
    const bool zipConOc = datos.is_object() && datos.value("oc", false);

    const std::string clavePack = packVersion::current();
    const bool overclock = packConOverclock();
    const bool ocBloquea = overclock && !zipConOc;
    const bool packOk = packProbado(datos, clavePack);
    /* Lo que impide instalar, en el orden en que conviene decirlo. Vacio = se puede. */
    auto bloqueo = [conAms, fwOk, ocBloquea, packOk, erista]() -> std::string {
        if (!conAms) return "menus/nexus/sin_ams"_i18n;
        if (!fwOk) return "menus/nexus/fw_no"_i18n;
        if (ocBloquea) return "oc";
        if (!packOk) return "menus/nexus/pack_no_probado"_i18n;
        if (!erista && !existe(MARIKO_PAYLOAD_PATH)) return "menus/errors/mariko_payload_missing"_i18n;
        return "";
    };
    auto intentar = [bloqueo, url, publicada, erista, zipLocal, reinstalarPack]() {
        const std::string b = bloqueo();
        if (b == "oc") ofrecerPackNormal(reinstalarPack);
        else if (!b.empty()) aviso(b);
        else instalar(url, publicada, erista, zipLocal);
    };

    if (!activo) {
        /* Sin Nexus: que hace con el boton al lado; o, si un pack lo apago, la
           franja para reactivarlo. Despues, si la consola sirve. */
        if (hayRegistro && !usbEnIni) {
            const size_t n = juegosEnUsb().size();
            this->addView(new Franja("menus/nexus/franja_desactivado"_i18n,
                                     n ? fmt::format("menus/nexus/franja_desactivado_resto"_i18n, n) : "menus/nexus/franja_desactivado_resto_sin"_i18n, true));
            PackListItem* re = new PackListItem("menus/nexus/reactivar_boton"_i18n);
            re->setWarningValue(" ");
            re->getClickEvent()->subscribe([intentar](brls::View*) { intentar(); });
            this->addView(re);
        }
        else {
            const std::string tam = texto(datos, "tamano");
            this->addView(new Presentacion(tam.empty() ? "menus/nexus/respaldo_nota_sin"_i18n : fmt::format("menus/nexus/respaldo_nota"_i18n, tam)));
            PackListItem* inst = new PackListItem("menus/nexus/instalar"_i18n);
            inst->setWarningValue(" ");
            inst->getClickEvent()->subscribe([intentar](brls::View*) { intentar(); });
            this->addView(inst);
        }

        this->addView(new HeaderOqb("menus/nexus/tu_consola"_i18n, 3));
        PackListItem* itemFw = new PackListItem("menus/nexus/req_firmware"_i18n + " " + (fw.empty() ? "?" : fw), "",
                                                fwOk ? "menus/nexus/probado"_i18n : "menus/nexus/fw_no"_i18n);
        if (fwOk) itemFw->setPositiveValue("menus/nexus/compatible"_i18n);
        else itemFw->setWarningValue("menus/nexus/todavia_no"_i18n);
        this->addView(itemFw);

        std::string nombreBase = clavePack.substr(0, clavePack.rfind(".zip"));
        std::replace(nombreBase.begin(), nombreBase.end(), '-', ' ');
        std::string subPack = !packOk ? "menus/nexus/pack_no_probado_sub"_i18n
                              : overclock ? (zipConOc ? "menus/nexus/pack_oc_ok_sub"_i18n : "menus/nexus/pack_oc_sub"_i18n)
                                          : "menus/nexus/probado"_i18n;
        PackListItem* itemPack = new PackListItem("menus/nexus/req_pack"_i18n + " " + (nombreBase.empty() ? "menus/nexus/pack_desconocido"_i18n : nombreBase), "", subPack);
        if (conAms && !ocBloquea && packOk) itemPack->setPositiveValue("menus/nexus/compatible"_i18n);
        else itemPack->setWarningValue("menus/nexus/todavia_no"_i18n);
        if (ocBloquea) itemPack->getClickEvent()->subscribe([reinstalarPack](brls::View*) { ofrecerPackNormal(reinstalarPack); });
        this->addView(itemPack);

        PackListItem* itemDisco = new PackListItem("menus/nexus/req_disco"_i18n, "", "menus/nexus/req_disco_sub"_i18n);
        itemDisco->setValue("exFAT", false, false);
        this->addView(itemDisco);
    }
    else {
        /* Avisos arriba de todo, como franja con su boton. */
        if (overclock && !reg.value("oc", false)) {
            this->addView(new Franja("menus/nexus/franja_oc"_i18n, "menus/nexus/franja_oc_resto"_i18n, true));
            PackListItem* re = new PackListItem(zipConOc ? "menus/nexus/reinstalar_boton"_i18n : "menus/nexus/pack_oc_instalar"_i18n);
            re->setWarningValue(" ");
            if (zipConOc) re->getClickEvent()->subscribe([intentar](brls::View*) { intentar(); });
            else re->getClickEvent()->subscribe([reinstalarPack](brls::View*) { ofrecerPackNormal(reinstalarPack); });
            this->addView(re);
        }
        else if (!publicada.empty() && texto(reg, "version") != publicada) {
            this->addView(new Franja("menus/nexus/franja_nueva"_i18n, "menus/nexus/franja_nueva_resto"_i18n, false));
            PackListItem* nueva = new PackListItem("menus/nexus/actualizar_boton"_i18n);
            nueva->setWarningValue(" ");
            nueva->getClickEvent()->subscribe([intentar](brls::View*) { intentar(); });
            this->addView(nueva);
        }

        // Tu USB y la SD; debajo, lo de todos los dias.
        this->addView(new TarjetaUsb());
        PackListItem* abrir = new PackListItem("menus/nexus/abrir"_i18n, "", "menus/nexus/abrir_sub2"_i18n);
        if (!existe(NRO)) abrir->setWarningValue("menus/nexus/falta"_i18n);
        abrir->getClickEvent()->subscribe([intentar](brls::View*) {
            if (existe(NRO)) util::abrirAppInstalada(NRO, "Nexus Bacán");
            else intentar();
        });
        this->addView(abrir);
        brls::ListItem* eject = new brls::ListItem("menus/nexus/expulsar"_i18n, "", "menus/nexus/expulsar_sub"_i18n);
        eject->getClickEvent()->subscribe([](brls::View*) { expulsar(); });
        this->addView(eject);

        // Lo instalado, de un vistazo.
        const Estado est = leerEstado();
        std::vector<std::pair<std::string, bool>> items = {
            {"menus/nexus/est_ams"_i18n, est.archivo},
            {"menus/nexus/est_app"_i18n, existe(NRO)},
            {"menus/nexus/est_acceso"_i18n, existe(NRO) && fwd::exists(NRO)},
            {"menus/nexus/est_overlay"_i18n, existe(OVL) && existe(ULTRAHAND)},
        };
        if (overclock) items.push_back({"menus/nexus/est_oc"_i18n, reg.value("oc", false)});
        this->addView(new FilaEstado(items));

        // Lo de vez en cuando.
        this->addView(new HeaderOqb("menus/nexus/mantenimiento"_i18n, 3));
        PackListItem* re = new PackListItem("menus/nexus/reinstalar_corto"_i18n, "", "menus/nexus/reinstalar_sub2"_i18n);
        re->getClickEvent()->subscribe([intentar](brls::View*) { intentar(); });
        this->addView(re);
        if (existe(NRO)) {
            /* Sin acceso: crearlo (en dorado). Con acceso: rehacerlo con el ícono y el nombre actuales. */
            const bool tiene = fwd::exists(NRO);
            PackListItem* acceso = new PackListItem(tiene ? "menus/nexus/acceso_rehacer2"_i18n : "menus/nexus/acceso_crear"_i18n, "",
                                                    tiene ? "menus/nexus/acceso_rehacer_sub2"_i18n : "menus/nexus/acceso_crear_sub"_i18n);
            if (!tiene) acceso->setWarningValue("menus/nexus/falta"_i18n);
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
        PackListItem* quitar = new PackListItem("menus/nexus/quitar"_i18n, "", "menus/nexus/quitar_sub"_i18n);
        quitar->setPeligro(true);
        quitar->getClickEvent()->subscribe([erista, reinstalarPack](brls::View*) { quitarNexus(erista, reinstalarPack); });
        this->addView(quitar);
    }

    this->addView(new HeaderOqb("menus/nexus/ayuda"_i18n, 3));
    brls::ListItem* guia = new brls::ListItem("menus/nexus/guia"_i18n, "", "menus/nexus/guia_sub2"_i18n);
    const nlohmann::ordered_json copia = datos;
    guia->getClickEvent()->subscribe([copia](brls::View*) { abrirGuia(copia); });
    this->addView(guia);

    brls::ListItem* falla = new brls::ListItem("menus/nexus/si_falla"_i18n, "", "menus/nexus/si_falla_sub"_i18n);
    falla->getClickEvent()->subscribe([](brls::View*) { abrirTexto("menus/nexus/si_falla"_i18n, "menus/nexus/si_falla_texto"_i18n); });
    this->addView(falla);

    brls::ListItem* diag = new brls::ListItem("menus/nexus/soporte"_i18n, "", "menus/nexus/soporte_sub"_i18n);
    diag->getClickEvent()->subscribe([](brls::View*) {
        abrirEnviarSoporte("OQB Updater", diagnostico, NexusTab::diagnosticoIncluye());
    });
    this->addView(diag);
}

std::string NexusTab::subtitulo()
{
    if (!instalado()) return "menus/nexus/subtitulo"_i18n;
    const nlohmann::ordered_json reg = registro();
    const std::string v = texto(reg, "version");
    if (v.empty()) return "menus/nexus/subtitulo"_i18n;
    return fmt::format(reg.value("oc", false) ? "menus/nexus/version_sub_oc"_i18n : "menus/nexus/version_sub"_i18n, v);
}

std::string NexusTab::diagnosticoSoporte() { return diagnostico(); }

std::vector<std::string> NexusTab::diagnosticoIncluye()
{
    return {"menus/soporte/inc_versiones"_i18n, "menus/soporte/inc_registros"_i18n, "menus/soporte/inc_juegos"_i18n,
            "menus/soporte/inc_modulos"_i18n};
}
