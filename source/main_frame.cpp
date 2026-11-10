#include "main_frame.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <json.hpp>
#include <map>
#include <set>
#include <vector>
#include <cctype>
#include <thread>

#include "about_tab.hpp"
#include "dialogo_centrado.hpp"
#include "current_cfw.hpp"
#include "iconos_oqb.hpp"
#include "inicio_tab.hpp"
#include "oqb_theme.hpp"
#include "pack_version.hpp"
#include "pagina_oqb.hpp"
#include "apps_page.hpp"
#include "trucos_page.hpp"
#include "arranque.hpp"
#include "ams_tab.hpp"
#include "app_update.hpp"
#include "estado_page.hpp"
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
    nlohmann::ordered_json packsGuardado;  // pack-versions.json, para el aviso de Inicio

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
        nlohmann::ordered_json packsObtenidos;
        std::thread h6([&] {
            packsObtenidos = packVersion::fetchPublished();
            arranque::marca("red: packs listos");
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
        h6.join();
        packsGuardado = packsObtenidos;
        /* Los iconos de las apps: aqui mismo, con la pantalla de carga a la
           vista y despues de las demas consultas. Sin hilo suelto: uno que
           seguia corriendo mientras se armaba la ventana tumbaba la 3.2. Solo
           baja algo si falta alguno (la primera vez, ~150 KB). */
        appUpdates::asegurarIconos(catalogoObtenido);
        tagGuardado = tagObtenido;
        nxlinksGuardado = nxObtenido;
        if (nxlinksGuardado.is_object()) {
            nxlinksGuardado[appUpdates::NXLINKS_KEY] = catalogoObtenido;
            nxlinksGuardado["_partidas"] = partidasObtenidas;
        }
        consultado = nxlinksGuardado.is_object() && !nxlinksGuardado.empty();
    }

    // "23.5.0" -> {23,5,0} para comparar.
    std::vector<int> partesVersion(const std::string& v)
    {
        std::vector<int> out;
        int n = 0;
        bool hay = false;
        for (char c : v + ".") {
            if (std::isdigit(static_cast<unsigned char>(c))) {
                n = n * 10 + (c - '0');
                hay = true;
            }
            else if (c == '.') {
                out.push_back(hay ? n : 0);
                n = 0;
                hay = false;
            }
            else
                break;
        }
        while (out.size() < 3) out.push_back(0);
        return out;
    }

    /* Generacion de llave maestra -> primer firmware que la usa. Sale de
       Atmosphère (pkg1_key_generation.hpp). El catalogo puede agregar las nuevas
       ("llaves_firmware": {"17": "24.0.0"}, en hex) sin sacar version. */
    std::map<int, std::string> tablaLlaves(const nlohmann::ordered_json& nxlinks)
    {
        std::map<int, std::string> t = {{0x00, "1.0.0"},  {0x01, "3.0.0"},  {0x02, "3.0.1"},  {0x03, "4.0.0"},  {0x04, "5.0.0"},  {0x05, "6.0.0"},
                                        {0x06, "6.2.0"},  {0x07, "7.0.0"},  {0x08, "8.1.0"},  {0x09, "9.0.0"},  {0x0A, "9.1.0"},  {0x0B, "12.1.0"},
                                        {0x0C, "13.0.0"}, {0x0D, "14.0.0"}, {0x0E, "15.0.0"}, {0x0F, "16.0.0"}, {0x10, "17.0.0"}, {0x11, "18.0.0"},
                                        {0x12, "19.0.0"}, {0x13, "20.0.0"}, {0x14, "21.0.0"}, {0x15, "22.0.0"}, {0x16, "23.0.0"}};
        if (nxlinks.is_object() && nxlinks.contains("llaves_firmware") && nxlinks["llaves_firmware"].is_object())
            for (const auto& [k, v] : nxlinks["llaves_firmware"].items())
                if (v.is_string()) t[std::strtol(k.c_str(), nullptr, 16)] = v.get<std::string>();
        return t;
    }

    // La generacion de llave mas alta con valor en un prod.keys (-1 si no hay).
    int llaveMasAlta(const std::string& ruta)
    {
        std::ifstream f(ruta);
        std::string linea;
        int max = -1;
        while (std::getline(f, linea)) {
            const auto p = linea.find("master_key_");
            if (p == std::string::npos || p != linea.find_first_not_of(" \t")) continue;
            const std::string idx = linea.substr(p + 11, 2);
            const auto igual = linea.find('=');
            if (idx.size() != 2 || !std::isxdigit(static_cast<unsigned char>(idx[0])) || !std::isxdigit(static_cast<unsigned char>(idx[1])) || igual == std::string::npos)
                continue;
            std::string valor = linea.substr(igual + 1);
            valor.erase(std::remove_if(valor.begin(), valor.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), valor.end());
            if (valor.size() != 32 || valor.find_first_not_of('0') == std::string::npos) continue;
            max = std::max(max, (int)std::strtol(idx.c_str(), nullptr, 16));
        }
        return max;
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
    /* Rediseno: sin la barra de titulo. El logo y el nombre van arriba del panel
       izquierdo (los dibuja draw) y la seccion usa toda la altura. */
    this->setTitle("");

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

    /* 4.0: las pestanas ocultas ya no existen (la opcion se reemplazo por Temas).
       Se ignora lo que haya quedado guardado: si no, quien oculto algo antes ya
       no tendria como volver a verlo. */
    json hideStatus = json::object();
    nlohmann::ordered_json nxlinks;
    if (conectado)
        nxlinks = nxlinksGuardado;
    if (nxlinks.is_object() && nxlinks.contains("enlaces") && nxlinks["enlaces"].is_object()) {
        const auto& enlaces = nxlinks["enlaces"];
        if (enlaces.contains("donar") && enlaces["donar"].is_string() && !enlaces["donar"].get<std::string>().empty())
            this->donationUrl = enlaces["donar"].get<std::string>();
    }

    bool erista = util::isErista();

    /* Rediseno: seis secciones con iconos (Inicio, Packs, Apps, Bacán Shop,
       Nextendo, Herramientas) en vez de nueve. Estado queda a un boton desde
       Inicio, Firmwares dentro de Packs e Idioma en Herramientas. */
    const bool conBacan = !util::getBoolValue(hideStatus, "apps") && BacanShopTab::available(nxlinks);
    const bool conTrucos = !util::getBoolValue(hideStatus, "cheats");
    const bool conNextendo = !util::getBoolValue(hideStatus, "apps") && NextendoTab::available(nxlinks);
    const bool nextendoActivo = protection::failedOnlyByNextendo(checks);

    // Lo que muestra Inicio: todo de lo que ya se calculo, sin red ni SD aparte.
    InicioTab::Datos datos;
    {
        const std::string info = CurrentCfw::getAmsInfo();
        const auto barra = info.find('|');
        datos.ams = barra == std::string::npos ? "" : info.substr(0, barra);
        if (barra != std::string::npos) datos.arranque = info.substr(barra + 1) == "E" ? "emuNAND" : "sysNAND";
        datos.firmware = fmt::format("{}.{}.{}", HOSVER_MAJOR(hosversionGet()), HOSVER_MINOR(hosversionGet()), HOSVER_MICRO(hosversionGet()));
        s64 libre = 0;
        if (R_SUCCEEDED(fs::getFreeStorageSD(libre))) datos.libre = fmt::format("{:.0f} GB", libre / 1073741824.0);
        const std::string clave = packVersion::current();
        if (!clave.empty()) {
            std::string nombre = clave.substr(0, clave.rfind(".zip"));
            std::replace(nombre.begin(), nombre.end(), '-', ' ');
            datos.pack = nombre;
            const std::string tengo = packVersion::installed(clave);
            const std::string publicada = packVersion::published(packsGuardado, clave).date;
            if (!publicada.empty() && !tengo.empty() && publicada != tengo) {
                datos.packNuevo = true;
                datos.packNuevoNombre = nombre;
                datos.packNuevoFecha = publicada;
            }
        }
        datos.appVersion = AppVersion;
        datos.appTag = conectado ? tagGuardado : "";
        datos.appNueva = appUpdate::isAvailable(datos.appTag);
        const nlohmann::ordered_json instaladas = appUpdates::installed();
        if (nxlinks.is_object() && nxlinks.contains("apps") && nxlinks["apps"].is_object()) {
            std::vector<std::string> nombres;
            for (const auto& [nombre, url] : nxlinks["apps"].items()) {
                if (url.is_string() && appUpdates::hasUpdate(nxlinks, instaladas, url.get<std::string>())) {
                    ++datos.appsNuevas;
                    nombres.push_back(nombre);
                }
            }
            for (size_t i = 0; i < nombres.size() && i < 2; ++i) datos.appsNombres += (i ? ", " : "") + nombres[i];
            if (nombres.size() > 2) datos.appsNombres += fmt::format("menus/inicio/y_mas"_i18n, nombres.size() - 2);
        }
        datos.hayTrucos = conTrucos;
        // Lo que reviso Trucos la ultima vez (su cache): sin cache, se invita a entrar.
        {
            std::ifstream f(std::string(CONFIG_PATH) + "trucos_juegos.json");
            const nlohmann::json cache = f ? nlohmann::json::parse(f, nullptr, false) : nlohmann::json();
            if (cache.is_object() && cache.contains("juegos") && cache["juegos"].is_array()) {
                datos.trucosListos = 0;
                for (const auto& g : cache["juegos"]) {
                    const int e = g.is_object() ? g.value("estado", 3) : 3;
                    if (e == 0) ++datos.trucosListos;
                    else if (e == 1 || e == 2) ++datos.trucosOtra;
                }
            }
        }
        // Partidas completas para los juegos instalados.
        if (nxlinks.is_object() && nxlinks.contains("_partidas") && nxlinks["_partidas"].is_object() && nxlinks["_partidas"].contains("partidas") &&
            nxlinks["_partidas"]["partidas"].is_array()) {
            std::set<u64> instalados;
            std::vector<NsApplicationRecord> registros(64000);
            s32 total = 0;
            if (R_SUCCEEDED(nsListApplicationRecord(registros.data(), registros.size(), 0, &total)))
                for (s32 i = 0; i < total; ++i) instalados.insert(registros[i].application_id);
            std::set<u64> conPartida;
            for (const auto& p : nxlinks["_partidas"]["partidas"])
                if (p.is_object() && p.contains("id") && p["id"].is_string()) {
                    const u64 id = std::strtoull(p["id"].get<std::string>().c_str(), nullptr, 16);
                    if (instalados.count(id)) conPartida.insert(id);
                }
            datos.partidas = conPartida.size();
        }
        // prod.keys: si estan y de que firmware son, comparado con el de la consola.
        {
            const char* rutas[] = {"/switch/prod.keys", "/prod.keys"};
            std::string ruta;
            for (const char* r : rutas)
                if (util::existeEnSd(r)) {
                    ruta = r;
                    break;
                }
            if (ruta.empty()) {
                datos.llavesEstado = 2;
                datos.llavesTexto = "menus/inicio/llaves_no"_i18n;
            }
            else {
                const auto tabla = tablaLlaves(nxlinks);
                const int tengo = llaveMasAlta(ruta);
                const auto consola = partesVersion(datos.firmware);
                int necesita = -1;
                for (const auto& [gen, fw] : tabla)
                    if (partesVersion(fw) <= consola) necesita = std::max(necesita, gen);
                const auto suya = tabla.find(tengo);
                if (tengo < 0 || suya == tabla.end()) {
                    datos.llavesEstado = 3;
                    datos.llavesTexto = "menus/inicio/llaves_hay"_i18n;
                }
                else if (necesita > tengo) {
                    datos.llavesEstado = 1;
                    datos.llavesTexto = fmt::format("menus/inicio/llaves_viejas"_i18n, suya->second);
                }
                else {
                    datos.llavesEstado = 0;
                    datos.llavesTexto = fmt::format("menus/inicio/llaves_ok_hasta"_i18n, suya->second);
                }
            }
        }
        /* Aviso de firmware nuevo que Atmosphère no soporta todavia. Lo manda el
           catalogo ("firmware_aviso": {"version": "23.5.0", "soportado": false}). */
        if (nxlinks.is_object() && nxlinks.contains("firmware_aviso") && nxlinks["firmware_aviso"].is_object()) {
            const auto& av = nxlinks["firmware_aviso"];
            const std::string v = av.contains("version") && av["version"].is_string() ? av["version"].get<std::string>() : "";
            const bool soportado = av.contains("soportado") && av["soportado"].is_boolean() && av["soportado"].get<bool>();
            if (!v.empty() && !soportado) {
                if (partesVersion(datos.firmware) >= partesVersion(v)) {
                    datos.fwAvisoGrave = true;
                    datos.fwAviso = fmt::format("menus/inicio/fw_aviso_en"_i18n, datos.firmware);
                }
                else
                    datos.fwAviso = fmt::format("menus/inicio/fw_aviso_antes"_i18n, v);
            }
        }
        datos.hayNextendo = conNextendo;
        datos.nextendoFunciona = conNextendo && NextendoTab::working(nxlinks, nextendoActivo);
        for (const auto& c : checks) {
            if (c.status == protection::Status::Fail && !c.byNextendo) datos.proteccionFalla = true;
            else if (c.status != protection::Status::Ok) ++datos.avisos;
        }
    }

    View* vistaPacks = nullptr;
    View* vistaApps = nullptr;
    View* vistaTrucos = nullptr;
    View* vistaBacan = nullptr;
    View* vistaNextendo = nullptr;
    auto entrada = [this](iconos::Icono icono) {
        brls::View* item = this->sidebar->getChild(this->sidebar->getViewsCount() - 1);
        this->iconosMenu.push_back({item, icono});
        return item;
    };

    if (!util::getBoolValue(hideStatus, "about")) {
        const std::string donar = this->donationUrl;
        const std::vector<protection::Check> copia = checks;
        const bool packNuevo = datos.packNuevo, appNueva = datos.appNueva;
        const std::string clavePack = packVersion::current();
        const int llavesEstado = datos.llavesEstado;
        const std::string firmwareConsola = datos.firmware;
        const bool esErista = util::isErista();
        auto ir = [this, tag, donar, copia, conBacan, packNuevo, appNueva, clavePack, llavesEstado, firmwareConsola, esErista](InicioTab::Destino d) {
            switch (d) {
                case InicioTab::Destino::Estado: {
                    // Rediseno: protecciones, Cliente OQB y apoyo (los datos de la consola ya estan en Inicio).
                    brls::Application::pushView(new EstadoFrame(donar, copia));
                    return;
                }
                case InicioTab::Destino::Trucos:
                    if (conBacan) {
                        trucos::abrir();
                        return;
                    }
                    this->irA(this->itemTrucos);
                    return;
                case InicioTab::Destino::Packs:
                    // Con version nueva, directo a actualizar el pack que esta en la SD.
                    if (packNuevo && this->tabPacks && this->tabPacks->instalarPorClave(clavePack)) return;
                    this->irA(this->itemPacks);
                    return;
                case InicioTab::Destino::App:
                    if (!tag.empty()) appUpdate::pushFlow(appNueva ? tag : std::string(AppVersion), appNueva);
                    return;
                case InicioTab::Destino::Apps: this->irA(this->itemApps); return;
                case InicioTab::Destino::Partidas: this->irA(this->itemBacan); return;
                case InicioTab::Destino::Nextendo: this->irA(this->itemNextendo); return;
                case InicioTab::Destino::Llaves: {
                    /* Explica, y solo reinicia en Lockpick_RCM Pro si se elige. */
                    const std::string payload = "/bootloader/payloads/Lockpick_RCM_Pro.bin";
                    std::string texto;
                    if (llavesEstado == 2) texto = "menus/inicio/llaves_explica_no"_i18n;
                    else if (llavesEstado == 1) texto = fmt::format("menus/inicio/llaves_explica_viejas"_i18n, firmwareConsola);
                    else texto = "menus/inicio/llaves_explica_ok"_i18n;
                    if (!util::existeEnSd(payload)) {
                        brls::Dialog* d = new brls::Dialog(texto + "\n\n" + "menus/inicio/llaves_sin_payload"_i18n);
                        d->addButton("menus/common/ok"_i18n, [d](brls::View*) { d->close(); });
                        d->setCancelable(true);
                        d->open();
                        return;
                    }
                    /* En Mariko (con chip) no se puede reiniciar directo a un payload:
                       la consola vuelve a hekate y Lockpick se elige en Payloads. */
                    brls::Dialog* d = new DialogoCentrado(texto + "\n\n" + (esErista ? "menus/inicio/llaves_pasos"_i18n : "menus/inicio/llaves_pasos_mariko"_i18n), 460);
                    d->addButton("menus/inicio/llaves_ahora_no"_i18n, [d](brls::View*) { d->close(); });
                    d->addButton(esErista ? "menus/inicio/llaves_reiniciar"_i18n : "menus/inicio/llaves_reiniciar_hekate"_i18n, [d, payload, esErista](brls::View*) {
                        d->close();
                        if (esErista) util::rebootToPayload(payload);
                        else util::shutDown(true);
                    });
                    d->setCancelable(true);
                    d->open();
                    return;
                }
            }
        };
        this->addTab("menus/inicio/menu"_i18n, new InicioTab(datos, ir));
        entrada(iconos::Icono::Inicio);
    }
    arranque::marca("ventana: pestaña inicio");

    if (!util::getBoolValue(hideStatus, "atmosphere")) {
        PaginaOqb::Opciones op;
        op.columnas = 2;
        op.botonA = "menus/inicio/boton_instalar"_i18n;
        op.botonAviso = "menus/inicio/boton_actualizar"_i18n;
        op.hintX = "menus/inicio/limpia_corto"_i18n;
        this->addTab("menus/inicio/menu_packs"_i18n,
                     vistaPacks = new PaginaOqb("menus/inicio/titulo_packs"_i18n, "menus/inicio/sub_packs"_i18n,
                                                this->tabPacks = new AmsTab_Regular(nxlinks, erista, !util::getBoolValue(hideStatus, "firmwares")), op));
        this->itemPacks = entrada(iconos::Icono::Packs);
    }
    arranque::marca("ventana: pestaña packs");

    /* Con Bacán Shop, los trucos van dentro de su seccion. Sin ella (sin
       conexion, o el catalogo no la trae) siguen en su pestana. */
    if (conTrucos && !conBacan) {
        this->addTab("menus/main/download_cheats"_i18n, vistaTrucos = new ListDownloadTab(contentType::cheats));
        this->itemTrucos = entrada(iconos::Icono::Trucos);
        arranque::marca("ventana: pestaña trucos");
    }

    if (!util::getBoolValue(hideStatus, "apps")) {
        this->addTab("menus/main/apps"_i18n, vistaApps = new AppsPage(nxlinks));
        this->itemApps = entrada(iconos::Icono::Apps);
        arranque::marca("ventana: pestaña apps");
        this->appsSidebar = this->itemApps;
        if (datos.appsNuevas)
            this->appsItem = this->appsSidebar;
    }

    /* Bacán Shop. Punto dorado si hay version nueva de la tienda o de los
       trucos, o si no esta instalada y nunca se abrio. */
    if (conBacan) {
        PaginaOqb::Opciones op;
        op.forma = PaginaOqb::Forma::Secciones;
        this->addTab("Bacán Shop", vistaBacan = new PaginaOqb("menus/inicio/titulo_bacan"_i18n, "menus/inicio/sub_bacan"_i18n,
                                                               new BacanShopTab(nxlinks, checks, conTrucos), op));
        this->itemBacan = entrada(iconos::Icono::Shop);
        arranque::marca("ventana: pestaña bacan shop");
        const std::string urlTienda = BacanShopTab::url(nxlinks);
        const bool instalada = appUpdates::isInstalled(nxlinks, urlTienda);
        this->bacanNueva = instalada && appUpdates::hasUpdate(nxlinks, appUpdates::installed(), urlTienda);
        this->bacanTrucos = conTrucos && BacanShopTab::trucosNuevos();
        this->bacanSidebar = this->itemBacan;
        if (this->bacanNueva || this->bacanTrucos || (!instalada && !BacanShopTab::seen()))
            this->bacanItem = this->bacanSidebar;
    }

    /* Nextendo: solo con la consola en modo Nextendo, que es cuando sirve. */
    if (conNextendo) {
        PaginaOqb::Opciones op;
        op.forma = PaginaOqb::Forma::Secciones;
        this->addTab("Nextendo", vistaNextendo = new PaginaOqb("NEXTENDO", "menus/inicio/sub_nextendo"_i18n, new NextendoTab(nxlinks, nextendoActivo), op));
        this->itemNextendo = entrada(iconos::Icono::Nextendo);
        arranque::marca("ventana: pestaña nextendo");
        this->nextendoFunciona = datos.nextendoFunciona;
        if (!NextendoTab::seen() || this->nextendoFunciona)
            this->nextendoItem = this->itemNextendo;
    }
    this->catalogo = nxlinks;
    this->genInstalaciones = appUpdates::generacionInstalaciones();
    (void)vistaPacks;
    (void)vistaApps;
    (void)vistaTrucos;
    (void)vistaBacan;
    (void)vistaNextendo;

    if (!util::getBoolValue(hideStatus, "tools")) {
        PaginaOqb::Opciones op;
        op.forma = PaginaOqb::Forma::Grupos;
        this->addTab("menus/main/tools"_i18n, new PaginaOqb("menus/inicio/titulo_tools"_i18n, "menus/inicio/sub_tools"_i18n,
                                                         new ToolsTab(tag, util::getValueFromKey(nxlinks, "payloads"), erista, hideStatus, nxlinks), op));
        entrada(iconos::Icono::Herramientas);
    }
    arranque::marca("ventana: pestaña ajustes");

    this->registerAction("", brls::Key::B, [this] { return true; });
}

void MainFrame::irA(brls::View* item)
{
    // Enfocar la entrada del menu cambia de seccion; despues el foco pasa al contenido.
    if (!item) return;
    brls::Application::giveFocus(item);
    if (auto* entrada = dynamic_cast<brls::SidebarItem*>(item))
        if (brls::View* vista = entrada->getAssociatedView())
            if (brls::View* foco = vista->getDefaultFocus()) brls::Application::giveFocus(foco);
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

/* La altura de la cabecera es un valor global del estilo (la usan todos los
   marcos). Solo este va sin ella: se pone en 0 mientras se acomoda y se dibuja,
   y se devuelve enseguida para los demas. */
void MainFrame::layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash)
{
    const unsigned cabecera = style->AppletFrame.headerHeightRegular;
    style->AppletFrame.headerHeightRegular = 0;
    brls::AppletFrame::layout(vg, style, stash);
    style->AppletFrame.headerHeightRegular = cabecera;
}

void MainFrame::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    {
        const unsigned cabecera = style->AppletFrame.headerHeightRegular;
        style->AppletFrame.headerHeightRegular = 0;
        brls::TabFrame::draw(vg, x, y, width, height, style, ctx);
        style->AppletFrame.headerHeightRegular = cabecera;
    }

    /* El nombre arriba del panel izquierdo, en Bebas Neue (OFL) y en dos lineas:
       "OQB" con la Trifuerza a su derecha, en el color del tema, y "UPDATER"
       debajo. En una sola linea, a esta altura, Bebas no entra en el panel. */
    {
        static int fuente = -2;
        if (fuente == -2) fuente = nvgCreateFont(vg, "bebas", "romfs:/fonts/BebasNeue-Regular.ttf");
        const float lx = x + style->Sidebar.marginLeft + 12.0f;
        const float tam = 56.0f;
        nvgFontFaceId(vg, fuente >= 0 ? fuente : ctx->fontStash->regular);
        nvgFontSize(vg, tam);
        nvgTextLetterSpacing(vg, 1.0f);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, this->a(ctx->theme->textColor));
        const float cy1 = y + 38.0f, cy2 = y + 86.0f;
        nvgBeginPath(vg);
        const float finOqb = nvgText(vg, lx, cy1, "OQB", nullptr);
        nvgBeginPath(vg);
        nvgText(vg, lx, cy2, "UPDATER", nullptr);
        nvgTextLetterSpacing(vg, 0.0f);

        /* La Trifuerza, del alto de las mayusculas de "OQB": en Bebas la H mide 0,70
           del tamano y, centrada la linea, la base queda 0,25 bajo el centro. La
           punta queda a la altura del borde de arriba de las letras. */
        const float th = tam * 0.70f, esc = th / 31.0f;
        const float tx = finOqb + 12.0f, ty = cy1 + tam * 0.25f - th;
        const float tris[3][6] = {{18, 0, 27, 15.5f, 9, 15.5f}, {27, 15.5f, 36, 31, 18, 31}, {9, 15.5f, 18, 31, 0, 31}};
        nvgFillColor(vg, this->a(oqb::acentoFuerte()));
        for (const auto& t : tris) {
            nvgBeginPath(vg);
            nvgMoveTo(vg, tx + t[0] * esc, ty + t[1] * esc);
            nvgLineTo(vg, tx + t[2] * esc, ty + t[3] * esc);
            nvgLineTo(vg, tx + t[4] * esc, ty + t[5] * esc);
            nvgClosePath(vg);
            nvgFill(vg);
        }
    }


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
