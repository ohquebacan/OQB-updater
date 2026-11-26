#include <vector>
#include <mutex>
#include "hekate_idioma.hpp"
#include "led.hpp"
#include <curl/curl.h>
#include <switch.h>

extern "C" u32 __nx_applet_exit_mode;

#include <atomic>
#include <borealis.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>
#include <json.hpp>

#include "app_update.hpp"
#include "arranque.hpp"
#include "constants.hpp"
#include "current_cfw.hpp"
#include "fs.hpp"
#include "icono_carga.hpp"
#include "donar_page.hpp"
#include "main_frame.hpp"
#include "cola.hpp"
#include "cola_page.hpp"
#include "confirm_page.hpp"
#include "worker_page.hpp"
#include "list_download_tab.hpp"
#include "ntp.hpp"
#include "oqb_theme.hpp"
#include "warning_page.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

/* Escribir el reloj necesita time:s; con el servicio de usuario, que es el que
   libnx coge por defecto, timeSetCurrentTime falla. Esta linea ya estaba en el
   archivo, comentada, desde el intento de NTP que quedo a medias. */
TimeServiceType __nx_time_service_type = TimeServiceType_System;

CFW CurrentCfw::running_cfw;

namespace {
    bool hayTraduccion(const std::string& locale)
    {
        return !locale.empty() && std::filesystem::exists(fmt::format(LOCALISATION_FILE, locale));
    }

    // es-419 -> es, pt-PT -> pt, en-GB -> en. Si tampoco hay, ingles.
    std::string resolverIdioma(const std::string& pedido)
    {
        if (hayTraduccion(pedido))
            return pedido;

        const size_t guion = pedido.find('-');
        if (guion != std::string::npos) {
            const std::string base = pedido.substr(0, guion);
            if (hayTraduccion(base))
                return base;
        }
        return "en-US";
    }

    /* Lo primero que se ve al abrir: fondo negro, la Trifuerza de la app PC
       encendiendose y el nombre en Upheaval, mientras se consulta internet en
       segundo plano. Antes la app armaba la ventana con la red de por medio y
       hasta entonces la pantalla quedaba en negro, que para el usuario es la
       app colgada. */
    class PantallaCarga : public brls::View
    {
    private:
        std::chrono::steady_clock::time_point inicio = std::chrono::steady_clock::now();
        bool vista = false;
        // Desde cuando ya no se dibuja (ver retirar()).
        bool retirada = false;
        std::chrono::steady_clock::time_point finRetiro{};
        int fuenteTitulo = -1;
        bool fuenteIntentada = false;

    public:
        // Segundos desde que se ve (la animacion empieza con el primer cuadro).
        float tiempo() const
        {
            return this->vista ? std::chrono::duration<float>(std::chrono::steady_clock::now() - this->inicio).count() : 0.0f;
        }

        /* Queda en la pila debajo de la ventana principal. Al volver de algo que
           se abrio directo encima de ella (las mejoras de Bacán Shop), borealis
           redibuja la ventana con un fundido y, por ser esta translucida, la
           Trifuerza se asomaba medio segundo. Se deja de dibujar en cuanto
           termina el fundido de entrada de la ventana principal. */
        void retirar(float trasSegundos)
        {
            this->retirada = true;
            this->finRetiro = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(trasSegundos * 1000));
        }

        // Los tres triangulos ya encendidos.
        bool encendida() const
        {
            return this->tiempo() >= duracionEncendido();
        }

        /* Translucida a proposito: asi la ventana principal entra encima con
           su propio fundido, sin que borealis tenga que ocultar esta antes.
           Ese ocultar terminaba aplicandose al aviso de version (que se abre
           enseguida) y la ventana principal quedaba invisible. Debajo de una
           vista opaca borealis no la dibuja, asi que no cuesta nada. */
        bool isTranslucent() override
        {
            return true;
        }

        void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override
        {
            if (this->retirada && std::chrono::steady_clock::now() >= this->finRetiro)
                return;
            if (!this->vista) {
                this->vista = true;
                this->inicio = std::chrono::steady_clock::now();
                arranque::marca("pantalla de carga visible");
            }
            if (!this->fuenteIntentada) {
                this->fuenteIntentada = true;
                this->fuenteTitulo = nvgCreateFont(vg, "upheaval", "romfs:/fonts/upheavtt.ttf");
            }

            const float t = this->tiempo();
            const float cx = x + width / 2.0f;
            const float cy = y + height / 2.0f - 70.0f;

            nvgBeginPath(vg);
            nvgRect(vg, x, y, width, height);
            nvgFillColor(vg, this->a(nvgRGB(0, 0, 0)));
            nvgFill(vg);

            // Resplandor esmeralda detras: crece con cada triangulo y, con los
            // tres encendidos, respira despacio si la red todavia tarda.
            const float avance = std::fmin(t / duracionEncendido(), 1.0f);
            const float respiro = avance < 1.0f ? avance : 0.85f + 0.15f * std::cos((t - duracionEncendido()) * 2.2f);
            nvgBeginPath(vg);
            nvgCircle(vg, cx, cy, 230.0f);
            NVGcolor brillo = oqb::acentoBase(true), sinBrillo = brillo;
            brillo.a = 46.0f / 255.0f * respiro;
            sinBrillo.a = 0.0f;
            nvgFillPaint(vg, nvgRadialGradient(vg, cx, cy, 20.0f, 230.0f, this->a(brillo), this->a(sinBrillo)));
            nvgFill(vg);

            drawTrifuerzaEncendido(vg, cx, cy, t, 3.4f);

            // El nombre, en Upheaval como en la app PC (tamanos multiplos de 20).
            const float yTitulo = cy + 100.0f;
            nvgFontFaceId(vg, this->fuenteTitulo >= 0 ? this->fuenteTitulo : ctx->fontStash->regular);
            nvgFontSize(vg, 60.0f);
            nvgTextLetterSpacing(vg, 3.0f);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
            nvgFillColor(vg, this->a(nvgRGB(238, 242, 227)));
            nvgBeginPath(vg);
            nvgText(vg, cx, yTitulo, "OQB UPDATER", nullptr);
            nvgTextLetterSpacing(vg, 0.0f);

            // Raya esmeralda y la version, discretas.
            nvgBeginPath(vg);
            nvgRect(vg, cx - 40.0f, yTitulo + 78.0f, 80.0f, 4.0f);
            nvgFillColor(vg, this->a(oqb::acentoBase(true)));
            nvgFill(vg);

            const std::string version = std::string("v") + APP_VERSION;
            nvgFontFaceId(vg, ctx->fontStash->regular);
            nvgFontSize(vg, 20.0f);
            nvgFillColor(vg, this->a(nvgRGB(126, 138, 128)));
            nvgBeginPath(vg);
            nvgText(vg, cx, yTitulo + 98.0f, version.c_str(), nullptr);
        }

        void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override {}
    };
}  // namespace

namespace {
    AppletHookCookie g_luzCookie{};
    // Avisos de otros hilos (el acceso directo creado aparte), para mostrarlos en el de la pantalla.
    std::mutex g_avisosCandado;
    std::vector<std::string> g_avisos;
    std::mutex g_accesoCandado;          // de a un acceso directo por vez
    std::atomic<int> g_creandoAccesos{0};  // al cerrar, se espera a que terminen
    /* El hilo que crea los accesos. Sin detach(): en la consola no esta soportado,
       lanzaba una excepcion y la app se cerraba de golpe (5.0.0). */
    std::thread g_hiloAcceso;
    bool g_exitBloqueado = true;  // appletLockExit hasta que se pida cerrar
}

int main(int argc, char* argv[])
{
    arranque::marca("inicio");
    // Init the app
    // El tema y las medidas propias. Sin el tema, borealis usa el suyo y toda
    // la interfaz sale en los colores del menu de la Switch; sin las medidas,
    // las siete pestanas no caben en la barra lateral.
    if (!brls::Application::init(APP_TITLE, oqb::style(), oqb::themeVariants())) {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }

    /* Rediseno: Instrument Sans (la del mockup) en toda la app. Lo que no tiene
       (los simbolos de los botones, chino, coreano) sale de las fuentes de la
       consola, que quedan de respaldo. */
    {
        brls::FontStash* fuentes = brls::Application::getFontStash();
        NVGcontext* vg = brls::Application::getNVGContext();
        const int instrument = nvgCreateFont(vg, "instrument", "romfs:/fonts/InstrumentSans-Medium.ttf");
        if (instrument >= 0) {
            for (int f : {fuentes->regular, fuentes->sharedSymbols, fuentes->standard, fuentes->korean, fuentes->schinese, fuentes->extSchinese, fuentes->tchinese})
                if (f > 0) nvgAddFallbackFontId(vg, instrument, f);
            fuentes->regular = instrument;
        }
    }

    /* La consola reporta el idioma con su variante regional: es-419 para
       "Espanol (Latinoamerica)", y tambien en-GB, fr-CA, pt-PT... La app solo
       trae es, en-US, fr, pt-BR. Cuando no hay carpeta para el codigo exacto,
       borealis no carga ninguna traduccion y todo sale en ingles, aunque la
       consola este en espanol. Antes de rendirse se prueba el idioma base. */
    nlohmann::ordered_json languageFile = fs::parseJsonFile(LANGUAGE_JSON);
    const std::string idiomaPedido = (languageFile.find("language") != languageFile.end())
                                         ? languageFile["language"].get<std::string>()
                                         : i18n::getCurrentLocale();
    const std::string idioma = resolverIdioma(idiomaPedido);

    if (idioma != idiomaPedido)
        brls::Logger::info("Sin traduccion para {}, se usa {}", idiomaPedido, idioma);

    i18n::loadTranslations(idioma);
    arranque::marca("interfaz y traducciones listas");
    hekateIdioma::fijarIdiomaApp(idioma);
    hekateIdioma::alAbrir();  // una sola vez: hekate en el idioma de la app

        // appletInitializeGamePlayRecording();

        // Setup verbose logging on PC
#ifndef __SWITCH__
    brls::Logger::setLogLevel(brls::LogLevel::DEBUG);
#endif

    /* Abierta desde su icono del menu HOME, la app corre como juego. Al
       cerrarla, libnx la cierra al menu HOME por su cuenta, en vez de volver al
       cargador del forwarder: ese regreso es lo que dejaba la consola en mal
       estado. */
    if (appletGetAppletType() == AppletType_Application)
        __nx_applet_exit_mode = 1;

    setsysInitialize();
    plInitialize(PlServiceType_User);
    nsInitialize();
    socketInitializeDefault();
    nxlinkStdio();
    /* curl se inicializa una vez aqui y se limpia al salir: init/cleanup por
       peticion no es thread-safe y se cruzaba con el hilo de la hora. */
    curl_global_init(CURL_GLOBAL_ALL);
    pmdmntInitialize();
    pminfoInitialize();
    splInitialize();
    romfsInit();
    // Si otra app OQB se cerró desde el HOME con la luz de los mandos prendida
    // (no alcanza a apagarla), aquí se apaga al abrir.
    led::apagar();

    /* Al cerrarla desde el HOME a mitad de una tarea, el sistema la terminaba
       al instante y la luz de los mandos quedaba prendida. Ahora el sistema
       avisa primero: se apaga la luz y se deja cerrar enseguida, igual que
       antes (no se cambia nada mas del cierre). */
    appletLockExit();
    appletHook(&g_luzCookie, [](AppletHookType hook, void*) {
        if (hook != AppletHookType_OnExitRequest)
            return;
        led::alSalir();
        g_exitBloqueado = false;
        appletUnlockExit();
    }, nullptr);

    CurrentCfw::running_cfw = CurrentCfw::getCFW();

    /* En hora desde el arranque. Con los servidores de Nintendo bloqueados la
       correccion automatica del sistema no funciona — va contra ntp.nintendo.net,
       que esta justo entre lo que se bloquea — asi que el reloj se atrasa solo.
       Va en segundo plano: al abrir la app no se puede esperar a la red. */
    ntp::syncInBackground();

    fs::createTree(CONFIG_PATH);

    brls::Logger::setLogLevel(brls::LogLevel::DEBUG);
    brls::Logger::debug("Start");

    bool seguir = true;
    PantallaCarga* cargaVisible = nullptr;
    if (std::filesystem::exists(HIDDEN_AIO_FILE)) {
        /* La red y las protecciones corren en un hilo; mientras, el bucle de
           la interfaz sigue y se ve la pantalla de carga. La ventana se arma
           aqui, en el hilo de la interfaz, cuando el hilo termina: armarla
           desde otro hilo o desde draw() es lo que rompio el intento anterior. */
        arranque::marca("servicios listos");
        PantallaCarga* carga = new PantallaCarga();
        cargaVisible = carga;
        brls::Application::pushView(carga);
        std::atomic<bool> listo{false};
        std::thread precarga([&listo] {
            MainFrame::precargar();
            listo = true;
        });
        /* Se espera tambien a que terminen de encenderse los tres triangulos
           (menos de un segundo): con la red rapida la ventana llegaba antes y
           la animacion se cortaba a medias. */
        while ((!listo || !carga->encendida()) && (seguir = brls::Application::mainLoop()))
            ;
        precarga.join();
    }

    if (seguir && std::filesystem::exists(HIDDEN_AIO_FILE)) {
        MainFrame* mainFrame = new MainFrame();
        brls::Application::pushView(mainFrame);
        // − abre la cola de descargas (pushView le habia puesto el contador de FPS).
        mainFrame->registerAction("menus/cola/titulo"_i18n, brls::Key::MINUS, [] {
            ColaPage::abrir();
            return true;
        }, true);  // sin pista: lo dice la pildora del pie («− Abrir cola»)
        if (cargaVisible)
            cargaVisible->retirar(1.0f);
        arranque::marca("ventana principal armada");
        arranque::guardar();

        /* El aviso de version nueva va aqui, despues de empujar la ventana: si
           se abriera desde el constructor, MainFrame se empuja encima y el aviso
           queda debajo sin que se vea. El tag ya lo consulto MainFrame, asi que
           no se pregunta dos veces. */
        appUpdate::showNoticeIfAvailable(mainFrame->getLatestTag());

        /* La invitacion a donar, de vez en cuando. Nunca junto con el aviso de
           version nueva: una ventana emergente por vez. La apertura se cuenta
           igual. */
        donar::showIfDue(appUpdate::isAvailable(mainFrame->getLatestTag()) ? "" : mainFrame->getDonationUrl());
    }
    else if (seguir) {
        brls::Application::pushView(new WarningPage("menus/main/launch_warning"_i18n));
    }

    /* Con la cola de descargas trabajando, + no cierra de una: pregunta. Se
       ataja antes de que mainLoop vea la ventana marcada para cerrar. */
    bool salirConfirmado = false;
    for (;;) {
        if (!salirConfirmado && cola::ocupada()) {
            GLFWwindow* ventana = glfwGetCurrentContext();
            if (ventana && glfwWindowShouldClose(ventana)) {
                glfwSetWindowShouldClose(ventana, GLFW_FALSE);
                brls::Dialog* d = new brls::Dialog("menus/cola/salir_pregunta"_i18n);
                d->addButton("menus/cola/seguir"_i18n, [d](brls::View*) { d->close(); });
                d->addButton("menus/cola/salir"_i18n, [d, &salirConfirmado](brls::View*) {
                    salirConfirmado = true;
                    d->close([] { brls::Application::quit(); });
                });
                d->setCancelable(true);
                d->open();
            }
        }
        /* Termino un port: su acceso directo se ofrece aqui, estes donde estes
           en la app (5.0.0). Si dice que si, se crea en la cola. */
        {
            std::lock_guard<std::mutex> l(g_avisosCandado);
            for (const auto& a : g_avisos) brls::Application::notify(a);
            g_avisos.clear();
        }
        // Fallo algo de la cola sin su pantalla a la vista: se avisa estes donde estes.
        cola::Hecho fallo;
        if (cola::siguienteFallo(fallo)) {
            brls::Dialog* d = new brls::Dialog(fmt::format("menus/cola/fallo"_i18n, fallo.nombre, fallo.error));
            d->addButton("menus/common/ok"_i18n, [d](brls::View*) { d->close(); });
            d->setCancelable(true);
            d->open();
        }
        cola::Oferta oferta;
        if (cola::siguienteOferta(oferta)) {
            const std::string texto = oferta.nros.size() > 1
                                          ? fmt::format("menus/cola/oferta_varios"_i18n, oferta.nombre, oferta.nros.size())
                                          : fmt::format("menus/cola/oferta_uno"_i18n, oferta.nombre);
            brls::Dialog* d = new brls::Dialog(texto);
            d->addButton("menus/common/no"_i18n, [d](brls::View*) { d->close(); });
            d->addButton("menus/common/yes"_i18n, [d, oferta](brls::View*) {
                d->close([oferta] {
                    /* Enseguida y aparte de la cola: si mientras se respondia ya
                       empezo la siguiente descarga, el acceso esperaba detras de
                       ella (y de todo lo que siguiera). Tarda uno o dos segundos;
                       avisa al terminar. */
                    const std::vector<std::string> nros = oferta.nros;
                    const std::string nombre = oferta.nombre;
                    ++g_creandoAccesos;
                    if (g_hiloAcceso.joinable()) g_hiloAcceso.join();  // el anterior ya termino o le falta poco
                    g_hiloAcceso = std::thread([nros, nombre]() {
                        std::lock_guard<std::mutex> uno(g_accesoCandado);
                        bool bien = true;
                        for (const auto& n : nros)
                            bien = ListDownloadTab::crearAcceso(n, true) == "menus/common/all_done"_i18n && bien;
                        std::lock_guard<std::mutex> l(g_avisosCandado);
                        g_avisos.push_back(fmt::format(bien ? "menus/cola/acceso_listo"_i18n : "menus/cola/acceso_fallo"_i18n, nombre));
                        --g_creandoAccesos;
                    });
                });
            });
            d->setCancelable(true);
            d->open();
        }
        if (!(seguir && brls::Application::mainLoop())) break;
        led::refrescar();  // el pulso del mando tiene fin: se renueva mientras siga
    }
    cola::detener();
    // Un acceso directo a medio crear se termina (son uno o dos segundos).
    if (g_hiloAcceso.joinable()) g_hiloAcceso.join();

    // Antes de cerrar servicios: el hilo de la hora todavia puede estar usando
    // la red y el servicio de time.
    ntp::waitForBackgroundSync();

    curl_global_cleanup();

    // Que la luz de los mandos no quede latiendo si se cierra a mitad de una tarea.
    led::alSalir();
    appletUnhook(&g_luzCookie);
    if (g_exitBloqueado)
        appletUnlockExit();

    romfsExit();
    splExit();
    pminfoExit();
    pmdmntExit();
    socketExit();
    nsExit();
    setsysExit();
    plExit();
    return EXIT_SUCCESS;
}
