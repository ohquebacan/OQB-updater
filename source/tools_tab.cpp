#include "tools_tab.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <filesystem>
#include <fstream>
#include <vector>

#include "JC_page.hpp"
#include "PC_page.hpp"
#include "app_page.hpp"
#include "app_update.hpp"
#include "cheats_page.hpp"
#include "confirm_page.hpp"
#include "constants.hpp"
#include "forwarder_page.hpp"
#include "fs.hpp"
#include "hide_tabs_page.hpp"
#include "net_page.hpp"
#include "ntp.hpp"
#include "payload_page.hpp"
#include "progress_event.hpp"
#include "protection.hpp"
#include "protection_page.hpp"
#include "utils.hpp"
#include "worker_page.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;
using json = nlohmann::ordered_json;

namespace {
    constexpr const char AppVersion[] = APP_VERSION;

    /* El tamano se mide con stat y no con std::filesystem: en esta consola
       std::filesystem no es de fiar con rutas de la SD (copy_file ya nos fallo
       en silencio en otra app). Devuelve -1 si no existe. */
    long long tamanoArchivo(const std::string& ruta)
    {
        struct stat st;
        if (stat(ruta.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
            return -1;
        return (long long)st.st_size;
    }

    // Suma recursiva de una carpeta. -1 si no existe.
    long long tamanoCarpeta(const std::string& ruta)
    {
        DIR* dir = opendir(ruta.c_str());
        if (!dir)
            return -1;

        long long total = 0;
        while (struct dirent* entrada = readdir(dir)) {
            const std::string nombre = entrada->d_name;
            if (nombre == "." || nombre == "..")
                continue;

            std::string hijo = ruta;
            if (!hijo.empty() && hijo.back() != '/')
                hijo += '/';
            hijo += nombre;

            struct stat st;
            if (stat(hijo.c_str(), &st) != 0)
                continue;

            if (S_ISDIR(st.st_mode)) {
                const long long sub = tamanoCarpeta(hijo);
                if (sub > 0) total += sub;
            }
            else {
                total += (long long)st.st_size;
            }
        }
        closedir(dir);
        return total;
    }

    std::string enUnidades(long long bytes)
    {
        if (bytes >= 1024LL * 1024 * 1024)
            return fmt::format("{:.1f} GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
        if (bytes >= 1024LL * 1024)
            return fmt::format("{:.1f} MB", (double)bytes / (1024.0 * 1024.0));
        if (bytes >= 1024)
            return fmt::format("{:.1f} KB", (double)bytes / 1024.0);
        return fmt::format("{} B", bytes);
    }

    // Lo ultimo del camino, para nombrar en el mensaje sin la ruta entera.
    std::string nombreDe(const std::string& ruta)
    {
        std::string limpia = ruta;
        while (limpia.size() > 1 && limpia.back() == '/')
            limpia.pop_back();

        const size_t barra = limpia.rfind('/');
        return barra == std::string::npos ? limpia : limpia.substr(barra + 1);
    }
}

ToolsTab::ToolsTab(const std::string& tag, const nlohmann::ordered_json& payloads, bool erista, const nlohmann::ordered_json& hideStatus) : brls::List()
{
    /* OQB: primero de la lista a proposito. Es lo que hay que mirar despues de
       actualizar el pack o de cambiar el package3, que es justo cuando se puede
       perder el bloqueo DNS sin que nada avise. */
    brls::ListItem* protectionCheck = new brls::ListItem("menus/tools/protection"_i18n);
    protectionCheck->getClickEvent()->subscribe([](brls::View* view) {
        brls::PopupFrame::open("menus/tools/protection"_i18n, new ProtectionPage(), "menus/tools/protection_desc"_i18n, "");
    });
    protectionCheck->setHeight(LISTITEM_HEIGHT);

    brls::ListItem* cheats = new brls::ListItem("menus/tools/cheats"_i18n);
    cheats->getClickEvent()->subscribe([](brls::View* view) {
        brls::PopupFrame::open("menus/cheats/menu"_i18n, new CheatsPage(), "", "");
    });
    cheats->setHeight(LISTITEM_HEIGHT);

    brls::ListItem* outdatedTitles = new brls::ListItem("menus/tools/outdated_titles"_i18n);
    outdatedTitles->getClickEvent()->subscribe([](brls::View* view) {
        brls::PopupFrame::open("menus/tools/outdated_titles"_i18n, new AppPage_OutdatedTitles(), "menus/tools/outdated_titles_desc"_i18n, "");
    });
    outdatedTitles->setHeight(LISTITEM_HEIGHT);

    brls::ListItem* JCcolor = new brls::ListItem("menus/tools/joy_cons"_i18n);
    JCcolor->getClickEvent()->subscribe([](brls::View* view) {
        brls::Application::pushView(new JCPage());
    });
    JCcolor->setHeight(LISTITEM_HEIGHT);

    brls::ListItem* PCcolor = new brls::ListItem("menus/tools/pro_cons"_i18n);
    PCcolor->getClickEvent()->subscribe([](brls::View* view) {
        brls::Application::pushView(new PCPage());
    });
    PCcolor->setHeight(LISTITEM_HEIGHT);

    brls::ListItem* rebootPayload = new brls::ListItem("menus/tools/inject_payloads"_i18n);
    rebootPayload->getClickEvent()->subscribe([](brls::View* view) {
        brls::PopupFrame::open("menus/tools/inject_payloads"_i18n, new PayloadPage(), "", "");
    });
    rebootPayload->setHeight(LISTITEM_HEIGHT);

    /* La hora se pone sola al abrir la app; esta entrada es para forzarla y,
       sobre todo, para ver que paso. La automatica es muda a proposito: no se
       interrumpe a nadie al arrancar para contarle que el reloj ya estaba bien. */
    brls::ListItem* syncTime = new brls::ListItem("menus/time/sync"_i18n);
    syncTime->getClickEvent()->subscribe([](brls::View* view) {
        auto message = std::make_shared<std::string>();

        brls::StagedAppletFrame* stagedFrame = new brls::StagedAppletFrame();
        stagedFrame->setTitle("menus/time/sync"_i18n);
        stagedFrame->addStage(new WorkerPage(stagedFrame, "menus/time/syncing"_i18n, [message]() {
            auto& progress = ProgressEvent::instance();
            progress.reset();

            const ntp::SyncResult result = ntp::sync();

            // El dialogo no se puede abrir desde este hilo, asi que el texto se
            // deja aqui y lo muestra la pagina siguiente.
            if (!result.ok) {
                *message = fmt::format("menus/time/failed"_i18n, result.detail);
            }
            else {
                if (result.alreadyInSync) {
                    *message = fmt::format("menus/time/already"_i18n, result.detail);
                }
                else {
                    *message = fmt::format("menus/time/done"_i18n, result.detail,
                                           result.drift > 0 ? result.drift : -result.drift,
                                           result.drift > 0 ? "menus/time/behind"_i18n : "menus/time/ahead"_i18n);
                }

                /* La parte que faltaba explicar. La consola solo muestra la
                   hora nueva si tiene puesta la sincronizacion por internet:
                   sin ella queda guardada y no se ve, y eso parecia que la
                   herramienta no hubiera hecho nada. */
                if (!result.userClockWritten && !result.autoCorrectionEnabled) {
                    *message += "\n\n" + "menus/time/needs_auto"_i18n;
                }
            }

            progress.setStep(progress.getMax());
        }));
        stagedFrame->addStage(new ConfirmPage_Deferred(stagedFrame, message));
        brls::Application::pushView(stagedFrame);
    });
    syncTime->setHeight(LISTITEM_HEIGHT);

    /* El interruptor de la consola, desde aqui. La app escribe el reloj de red
       pero no el de usuario, asi que la hora solo se ve con esta opcion puesta:
       tenerla a mano es la diferencia entre que la sincronizacion sirva o no.

       Nada de esto usa showDialogBoxBlocking: hace busy-wait y esto corre en el
       hilo de la UI, que se trabaria a si mismo. Los dialogos van encadenados
       por el callback de close(), como en la pagina de forwarders. */
    bool autoCorrection = false;
    const bool autoCorrectionRead =
        R_SUCCEEDED(setsysIsUserSystemClockAutomaticCorrectionEnabled(&autoCorrection));

    brls::ListItem* clockSync = new brls::ListItem("menus/time/system_sync"_i18n);
    clockSync->setHeight(LISTITEM_HEIGHT);
    clockSync->setValue(!autoCorrectionRead ? "menus/about/status_unknown"_i18n
                        : autoCorrection    ? "menus/time/system_sync_on"_i18n
                                            : "menus/time/system_sync_off"_i18n);
    clockSync->getClickEvent()->subscribe([autoCorrection, autoCorrectionRead](brls::View* view) {
        if (!autoCorrectionRead) {
            util::showDialogBoxInfo("menus/time/system_sync_unreadable"_i18n);
            return;
        }

        const bool turningOn = !autoCorrection;

        // Escribe, vuelve a leer y cuenta lo que quedo de verdad. Que la
        // funcion exista no garantiza que el sistema acepte el cambio, y dar
        // por bueno lo que no se comprobo es el error que ya cometimos una vez
        // con el reloj de usuario.
        auto applyAndReport = [turningOn]() {
            setsysSetUserSystemClockAutomaticCorrectionEnabled(turningOn);

            bool now = false;
            if (R_FAILED(setsysIsUserSystemClockAutomaticCorrectionEnabled(&now))) {
                util::showDialogBoxInfo("menus/time/system_sync_unreadable"_i18n);
                return;
            }

            if (now == turningOn) {
                util::showDialogBoxInfo(turningOn ? "menus/time/system_sync_now_on"_i18n
                                                  : "menus/time/system_sync_now_off"_i18n);
            }
            else {
                util::showDialogBoxInfo("menus/time/system_sync_refused"_i18n);
            }
        };

        /* El seguro. Activar esto hace que la consola intente contactar el NTP
           de Nintendo cada tanto, y lo unico que impide que esos intentos
           salgan es el bloqueo DNS. Asi que antes de activarlo se comprueba que
           ese bloqueo este puesto, en vez de darlo por hecho. Para apagarlo no
           hace falta: apagarlo solo quita intentos. */
        if (turningOn && !protection::dnsBlockingOk(protection::run())) {
            brls::Dialog* warn = new brls::Dialog("menus/time/system_sync_unprotected"_i18n);
            warn->addButton("menus/time/system_sync_anyway"_i18n, [warn, applyAndReport](brls::View* v) {
                warn->close(applyAndReport);
            });
            warn->addButton("menus/common/no"_i18n, [warn](brls::View* v) { warn->close(); });
            warn->setCancelable(false);
            warn->open();
            return;
        }

        brls::Dialog* confirm = new brls::Dialog(turningOn ? "menus/time/system_sync_ask_on"_i18n
                                                           : "menus/time/system_sync_ask_off"_i18n);
        confirm->addButton("menus/common/yes"_i18n, [confirm, applyAndReport](brls::View* v) {
            confirm->close(applyAndReport);
        });
        confirm->addButton("menus/common/no"_i18n, [confirm](brls::View* v) { confirm->close(); });
        confirm->setCancelable(false);
        confirm->open();
    });

    brls::ListItem* netSettings = new brls::ListItem("menus/tools/internet_settings"_i18n);
    netSettings->getClickEvent()->subscribe([](brls::View* view) {
        brls::PopupFrame::open("menus/tools/internet_settings"_i18n, new NetPage(), "", "");
    });
    netSettings->setHeight(LISTITEM_HEIGHT);

    brls::ListItem* browser = new brls::ListItem("menus/tools/browser"_i18n);
    browser->getClickEvent()->subscribe([](brls::View* view) {
        std::string url;
        if (brls::Swkbd::openForText([&url](std::string text) { url = text; }, "cheatslips.com e-mail", "", 256, "https://duckduckgo.com", 0, "Submit", "https://website.tld")) {
            util::openWebBrowser(url);
        }
    });
    browser->setHeight(LISTITEM_HEIGHT);

    brls::ListItem* move = new brls::ListItem("menus/tools/batch_copy"_i18n);
    move->getClickEvent()->subscribe([](brls::View* view) {
        chdir("/");
        std::string error = "";
        if (std::filesystem::exists(COPY_FILES_TXT)) {
            error = fs::copyFiles(COPY_FILES_TXT);
        }
        else {
            error = "menus/tools/batch_copy_config_not_found"_i18n;
        }
        util::showDialogBoxInfo(error);
    });
    move->setHeight(LISTITEM_HEIGHT);

    brls::ListItem* cleanUp = new brls::ListItem("menus/tools/clean_up"_i18n);
    cleanUp->getClickEvent()->subscribe([](brls::View* view) {
        /* Antes borraba a ciegas y siempre decia "Finalizado", hubiera liberado
           11 MB o nada. Ahora se mide antes de borrar y se comprueba que de
           verdad desaparecio, para poder decir que paso. */
        long long liberado = 0;
        std::vector<std::string> borrados;

        for (const char* archivo : {AMS_FILENAME, APP_FILENAME, FIRMWARE_FILENAME,
                                    CHEATS_FILENAME, BOOTLOADER_FILENAME, CHEATS_VERSION,
                                    CUSTOM_FILENAME}) {
            const long long tam = tamanoArchivo(archivo);
            if (tam < 0)
                continue;

            std::error_code ec;
            std::filesystem::remove(archivo, ec);

            // No se confia en lo que devuelve: se comprueba que ya no esta.
            if (tamanoArchivo(archivo) < 0) {
                liberado += tam;
                borrados.push_back(nombreDe(archivo));
            }
        }

        for (const char* carpeta : {AMS_DIRECTORY_PATH, SEPT_DIRECTORY_PATH, FW_DIRECTORY_PATH}) {
            const long long tam = tamanoCarpeta(carpeta);
            if (tam < 0)
                continue;

            fs::removeDir(carpeta);

            if (tamanoCarpeta(carpeta) < 0) {
                liberado += tam;
                borrados.push_back(nombreDe(carpeta) + "/");
            }
        }

        if (borrados.empty()) {
            util::showDialogBoxInfo("menus/tools/clean_up_nothing"_i18n);
            return;
        }

        std::string lista;
        for (const std::string& nombre : borrados) {
            if (!lista.empty())
                lista += ", ";
            lista += nombre;
        }

        util::showDialogBoxInfo(fmt::format("menus/tools/clean_up_done"_i18n,
                                            enUnidades(liberado),
                                            borrados.size(),
                                            lista));
    });
    cleanUp->setHeight(LISTITEM_HEIGHT);

    brls::ListItem* language = new brls::ListItem("menus/tools/language"_i18n);
    language->getClickEvent()->subscribe([](brls::View* view) {
        std::vector<std::pair<std::string, std::string>> languages{
            std::make_pair("American English ({})", "en-US"),
            std::make_pair("日本語 ({})", "ja"),
            std::make_pair("Français ({})", "fr"),
            std::make_pair("Deutsch ({})", "de"),
            std::make_pair("Italiano ({})", "it"),
            std::make_pair("Español ({})", "es"),
            std::make_pair("Português ({})", "pt-BR"),
            std::make_pair("Nederlands ({})", "nl"),
            std::make_pair("Русский ({})", "ru"),
            std::make_pair("Română ({})", "ro"),
            std::make_pair("한국어 ({})", "ko"),
            std::make_pair("Polski ({})", "pl"),
            std::make_pair("简体中文 ({})", "zh-CN"),
            std::make_pair("繁體中文 ({})", "zh-TW"),
            std::make_pair("English (Great Britain) ({})", "en-GB"),
            std::make_pair("Français (Canada) ({})", "fr-CA"),
            std::make_pair("Español (Latinoamérica) ({})", "es-419"),
            std::make_pair("Português brasileiro ({})", "pt-BR"),
            std::make_pair("Traditional Chinese ({})", "zh-Hant"),
            std::make_pair("Simplified Chinese ({})", "zh-Hans")};
        brls::AppletFrame* appView = new brls::AppletFrame(true, true);
        brls::List* list = new brls::List();
        brls::ListItem* listItem;
        listItem = new brls::ListItem(fmt::format("System Default ({})", i18n::getCurrentLocale()));
        listItem->registerAction("menus/tools/language"_i18n, brls::Key::A, [] {
            std::filesystem::remove(LANGUAGE_JSON);
            brls::Application::quit();
            return true;
        });
        list->addView(listItem);
        for (auto& language : languages) {
            if (std::filesystem::exists(fmt::format(LOCALISATION_FILE, language.second))) {
                listItem = new brls::ListItem(fmt::format(language.first, language.second));
                listItem->registerAction("menus/tools/language"_i18n, brls::Key::A, [language] {
                    json updatedLanguage = json::object();
                    updatedLanguage["language"] = language.second;
                    std::ofstream out(LANGUAGE_JSON);
                    out << updatedLanguage.dump();
                    brls::Application::quit();
                    return true;
                });
                list->addView(listItem);
            }
        }
        appView->setContentView(list);
        brls::PopupFrame::open("menus/tools/language"_i18n, appView, "", "");
    });
    language->setHeight(LISTITEM_HEIGHT);

    brls::ListItem* createForwarder = new brls::ListItem("menus/forwarders/create_title"_i18n);
    createForwarder->getClickEvent()->subscribe([](brls::View* view) {
        brls::Application::pushView(new ForwarderCreatePage());
    });
    createForwarder->setHeight(LISTITEM_HEIGHT);

    brls::ListItem* manageForwarders = new brls::ListItem("menus/forwarders/manage_title"_i18n);
    manageForwarders->getClickEvent()->subscribe([](brls::View* view) {
        brls::Application::pushView(new ForwarderManagePage());
    });
    manageForwarders->setHeight(LISTITEM_HEIGHT);

    brls::ListItem* hideTabs = new brls::ListItem("menus/tools/hide_tabs"_i18n);
    hideTabs->getClickEvent()->subscribe([](brls::View* view) {
        brls::PopupFrame::open("menus/tools/hide_tabs"_i18n, new HideTabsPage(), "", "");
    });
    hideTabs->setHeight(LISTITEM_HEIGHT);

    /* Siempre visible. Si hay version nueva ofrece actualizar; si ya esta al
       dia permite volver a descargar, que sirve para recoger un rebuild
       publicado bajo el mismo tag. El flujo vive en appUpdate porque tambien lo
       abre el aviso de arranque. */
    const bool hasUpdate = appUpdate::isAvailable(tag);
    const std::string targetTag = hasUpdate ? tag : std::string(AppVersion);

    brls::ListItem* updateApp = new brls::ListItem(
        hasUpdate ? fmt::format("menus/app_update/entry"_i18n, AppVersion, tag)
                  : fmt::format("menus/app_update/entry_again"_i18n, AppVersion));
    updateApp->setHeight(LISTITEM_HEIGHT);
    updateApp->getClickEvent()->subscribe([targetTag, hasUpdate](brls::View* view) {
        appUpdate::pushFlow(targetTag, hasUpdate);
    });

    /* Dieciseis entradas seguidas se leen como una lista plana donde hay que
       recorrerlo todo para encontrar algo. Agrupadas, se ve de un vistazo
       donde buscar.

       El encabezado se agrega solo si su grupo tiene algo visible: con
       hide_tabs.json cualquier entrada puede desaparecer, y un titulo sobre un
       grupo vacio queda como una raya suelta.

       Altura 34 en vez de los 44 del estilo: son varios grupos, y con la
       altura por omision lo que se gana en orden se pierde en tener que
       desplazarse mas. */
    const auto visible = [&hideStatus](const char* clave) {
        return !util::getBoolValue(hideStatus, clave);
    };

    const auto grupo = [this](const std::string& titulo, const std::vector<brls::View*>& entradas) {
        if (entradas.empty())
            return;

        brls::Header* cabecera = new brls::Header(titulo);
        cabecera->setHeight(34);
        this->addView(cabecera);

        for (brls::View* entrada : entradas)
            this->addView(entrada);
    };

    const auto siVisible = [&visible](std::vector<brls::View*>& destino, const char* clave, brls::View* vista) {
        if (visible(clave))
            destino.push_back(vista);
    };

    std::vector<brls::View*> app, juegos, consola, red, archivos, inicio;

    app.push_back(updateApp);  // siempre: sin actualizacion ofrece volver a descargar
    siVisible(app, "language", language);
    app.push_back(hideTabs);

    siVisible(juegos, "cheats", cheats);
    siVisible(juegos, "outdatedtitles", outdatedTitles);

    siVisible(consola, "protection", protectionCheck);
    if (erista) siVisible(consola, "rebootpayload", rebootPayload);
    siVisible(consola, "jccolor", JCcolor);
    siVisible(consola, "pccolor", PCcolor);

    siVisible(red, "netsettings", netSettings);
    siVisible(red, "browser", browser);
    siVisible(red, "synctime", syncTime);
    siVisible(red, "synctime", clockSync);

    siVisible(archivos, "move", move);
    siVisible(archivos, "cleanup", cleanUp);

    siVisible(inicio, "createforwarder", createForwarder);
    siVisible(inicio, "manageforwarders", manageForwarders);

    grupo("menus/tools/grupo_app"_i18n, app);
    grupo("menus/tools/grupo_juegos"_i18n, juegos);
    grupo("menus/tools/grupo_consola"_i18n, consola);
    grupo("menus/tools/grupo_red"_i18n, red);
    grupo("menus/tools/grupo_archivos"_i18n, archivos);
    grupo("menus/tools/grupo_inicio"_i18n, inicio);
}
