#include <switch.h>

#include <borealis.hpp>
#include <filesystem>
#include <json.hpp>

#include "app_update.hpp"
#include "constants.hpp"
#include "current_cfw.hpp"
#include "fs.hpp"
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
}  // namespace

int main(int argc, char* argv[])
{
    // Init the app
    // El tema y las medidas propias. Sin el tema, borealis usa el suyo y toda
    // la interfaz sale en los colores del menu de la Switch; sin las medidas,
    // las siete pestanas no caben en la barra lateral.
    if (!brls::Application::init(APP_TITLE, oqb::style(), oqb::themeVariants())) {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
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

        // appletInitializeGamePlayRecording();

        // Setup verbose logging on PC
#ifndef __SWITCH__
    brls::Logger::setLogLevel(brls::LogLevel::DEBUG);
#endif

    setsysInitialize();
    plInitialize(PlServiceType_User);
    nsInitialize();
    socketInitializeDefault();
    nxlinkStdio();
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

    if (std::filesystem::exists(HIDDEN_AIO_FILE)) {
        MainFrame* mainFrame = new MainFrame();
        brls::Application::pushView(mainFrame);

        /* El aviso de version nueva va aqui, despues de empujar la ventana: si
           se abriera desde el constructor, MainFrame se empuja encima y el aviso
           queda debajo sin que se vea. El tag ya lo consulto MainFrame, asi que
           no se pregunta dos veces. */
        appUpdate::showNoticeIfAvailable(mainFrame->getLatestTag());
    }
    else {
        brls::Application::pushView(new WarningPage("menus/main/launch_warning"_i18n));
    }

    while (brls::Application::mainLoop());

    // Antes de cerrar servicios: el hilo de la hora todavia puede estar usando
    // la red y el servicio de time.
    ntp::waitForBackgroundSync();

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
