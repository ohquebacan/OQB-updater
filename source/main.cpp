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

    while (seguir && brls::Application::mainLoop());

    // Antes de cerrar servicios: el hilo de la hora todavia puede estar usando
    // la red y el servicio de time.
    ntp::waitForBackgroundSync();

    curl_global_cleanup();

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
