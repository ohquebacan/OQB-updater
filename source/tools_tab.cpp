#include "led.hpp"
#include "tools_tab.hpp"
#include "pagina_oqb.hpp"

#include <algorithm>

#include <dirent.h>
#include <sys/stat.h>

#include <filesystem>
#include <fstream>
#include <memory>
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
#include "instalacion_limpia.hpp"
#include "limpieza_mac.hpp"
#include "home_rotos.hpp"
#include "limpiar_emunand.hpp"
#include "version_inicio.hpp"
#include "net_page.hpp"
#include "ntp.hpp"
#include "oqb_theme.hpp"
#include "payload_page.hpp"
#include "progress_event.hpp"
#include "protection.hpp"
#include "protection_page.hpp"
#include "diagnostics_page.hpp"
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

ToolsTab::ToolsTab(const std::string& tag, const nlohmann::ordered_json& payloads, bool erista, const nlohmann::ordered_json& hideStatus,
                   const nlohmann::ordered_json& nxlinks) : brls::List()
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

    brls::ListItem* netSettings = new brls::ListItem("menus/tools/internet_settings"_i18n);
    netSettings->getClickEvent()->subscribe([](brls::View* view) {
        brls::PopupFrame::open("menus/tools/internet_settings"_i18n, new NetPage(), "", "");
    });
    netSettings->setHeight(LISTITEM_HEIGHT);


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

        // CHEATS_VERSION no: es lo que dice que base de trucos hay en la SD (Trucos
        // pediria bajarla otra vez aunque los trucos sigan ahi).
        for (const char* archivo : {AMS_FILENAME, APP_FILENAME, FIRMWARE_FILENAME,
                                    CHEATS_FILENAME, BOOTLOADER_FILENAME, CUSTOM_FILENAME}) {
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

    /* Basura de macOS en la SD (archivos "._", .DS_Store, .Spotlight-V100...), con el
       mismo criterio que la app de PC. Se confirma, se limpia con la cascada del
       registro a la vista y al final se dice cuanto se quito. */
    brls::ListItem* cleanMac = new brls::ListItem("menus/tools/clean_mac"_i18n);
    cleanMac->getClickEvent()->subscribe([](brls::View* view) {
        brls::StagedAppletFrame* frame = new brls::StagedAppletFrame();
        frame->setTitle("menus/tools/clean_mac"_i18n);
        frame->addStage(new ConfirmPage(frame, "menus/tools/clean_mac_confirm"_i18n));
        auto mensaje = std::make_shared<std::string>();
        frame->addStage(new WorkerPage(frame, "menus/tools/clean_mac_trabajando"_i18n, [mensaje]() {
            const limpiezaMac::Resultado r = limpiezaMac::limpiar();
            *mensaje = limpiezaMac::resumen(r);
            ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
        }));
        frame->addStage(new ConfirmPage_Deferred(frame, mensaje));
        brls::Application::pushView(frame);
    });
    cleanMac->setHeight(LISTITEM_HEIGHT);

    /* Iconos del HOME que se quedan cargando (0,0 MB): registros de juegos
       cuyo contenido ya no esta o esta danado. Se buscan al tocar la opcion,
       se muestran antes de borrar y al final se dice cuantos se quitaron. */
    /* Limpiar la emuNAND (como «Clean emuMMC» de HATS-Tools): quita los juegos
       que una emuNAND recién hecha copió de la consola y, si se elige, sus
       partidas. Solo en emuNAND con su propia carpeta Nintendo. */
    brls::ListItem* limpiarEmu = new brls::ListItem("menus/tools/limpiar_emunand"_i18n, "", "menus/tools/limpiar_emunand_sub"_i18n);
    limpiarEmu->getClickEvent()->subscribe([](brls::View* view) {
        auto plan = std::make_shared<limpiarEmunand::Plan>(limpiarEmunand::armar());
        using limpiarEmunand::Donde;
        if (plan->donde == Donde::SysNand) {
            util::showDialogBoxInfo("menus/tools/limpiar_emunand_sysnand"_i18n);
            return;
        }
        if (plan->donde == Donde::SinCarpeta) {
            util::showDialogBoxInfo("menus/tools/limpiar_emunand_sin_carpeta"_i18n);
            return;
        }
        if (plan->donde != Donde::Emunand || plan->error) {
            util::showDialogBoxInfo("menus/tools/limpiar_emunand_error"_i18n);
            return;
        }
        if (plan->juegos.empty() && plan->partidas.empty()) {
            util::showDialogBoxInfo("menus/tools/limpiar_emunand_nada"_i18n);
            return;
        }
        auto abrir = [plan](bool partidas) {
            brls::StagedAppletFrame* frame = new brls::StagedAppletFrame();
            frame->setTitle("menus/tools/limpiar_emunand"_i18n);
            frame->addStage(new ConfirmPage(frame, partidas ? fmt::format("menus/tools/limpiar_emunand_confirm_partidas"_i18n, plan->juegos.size(), plan->partidas.size())
                                                            : fmt::format("menus/tools/limpiar_emunand_confirm_juegos"_i18n, plan->juegos.size())));
            auto mensaje = std::make_shared<std::string>();
            frame->addStage(new WorkerPage(frame, "menus/tools/limpiar_emunand_trabajando"_i18n, [mensaje, plan, partidas]() {
                const limpiarEmunand::Resultado r = limpiarEmunand::ejecutar(*plan, partidas);
                if (r.cambio && r.juegos == 0 && r.partidas == 0) {
                    *mensaje = "menus/tools/limpiar_emunand_cambio"_i18n;
                } else {
                    std::string t = fmt::format("menus/tools/limpiar_emunand_listo_juegos"_i18n, r.juegos, plan->juegos.size());
                    if (partidas) t += fmt::format("menus/tools/limpiar_emunand_listo_partidas"_i18n, r.partidas, plan->partidas.size());
                    if (r.quedanJuegos >= 0) t += fmt::format("menus/tools/limpiar_emunand_quedan"_i18n, r.quedanJuegos, r.quedanPartidas);
                    if (r.juegosMal || r.partidasMal || r.cambio) t += "menus/tools/limpiar_emunand_fallos"_i18n;
                    *mensaje = t;
                }
                ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
            }));
            frame->addStage(new ConfirmPage_Deferred(frame, mensaje));
            brls::Application::pushView(frame);
        };
        /* Corto: el diálogo crece con el texto y, con un párrafo más, los
           botones quedaban fuera de la pantalla. */
        std::string texto = fmt::format("menus/tools/limpiar_emunand_resumen"_i18n, plan->juegos.size(), plan->partidas.size());
        if (plan->conArchivos)
            texto += fmt::format("menus/tools/limpiar_emunand_con_archivos"_i18n, plan->conArchivos);
        if (plan->nexus)
            texto += fmt::format("menus/tools/limpiar_emunand_nexus"_i18n, plan->nexus);
        texto += "menus/tools/limpiar_emunand_elige"_i18n;
        brls::Dialog* dialogo = new brls::Dialog(texto);
        if (!plan->juegos.empty())
            dialogo->addButton("menus/tools/limpiar_emunand_solo_juegos"_i18n, [dialogo, abrir](brls::View*) { dialogo->close([abrir]() { abrir(false); }); });
        if (!plan->partidas.empty())
            dialogo->addButton("menus/tools/limpiar_emunand_con_partidas"_i18n, [dialogo, abrir](brls::View*) { dialogo->close([abrir]() { abrir(true); }); });
        dialogo->setCancelable(true);
        dialogo->open();
    });

    /* Quitar el aviso de actualización obligatoria (como Goldleaf): juegos que
       piden una versión más nueva que la instalada, por ejemplo al reinstalar
       solo el juego base. Se les pone la versión que de verdad tienen. */
    brls::ListItem* versionItem = new brls::ListItem("menus/tools/version_inicio"_i18n, "", "menus/tools/version_inicio_sub"_i18n);
    versionItem->getClickEvent()->subscribe([](brls::View* view) {
        const std::vector<versionInicio::Pendiente> lista = versionInicio::buscar();
        if (lista.empty()) {
            util::showDialogBoxInfo("menus/tools/version_inicio_nada"_i18n);
            return;
        }
        brls::StagedAppletFrame* frame = new brls::StagedAppletFrame();
        frame->setTitle("menus/tools/version_inicio"_i18n);
        frame->addStage(new ConfirmPage(frame, fmt::format("menus/tools/version_inicio_confirm"_i18n, lista.size(), versionInicio::listado(lista))));
        auto mensaje = std::make_shared<std::string>();
        frame->addStage(new WorkerPage(frame, "menus/tools/version_inicio_trabajando"_i18n, [mensaje, lista]() {
            const int bien = versionInicio::arreglar(lista);
            *mensaje = bien == static_cast<int>(lista.size()) ? fmt::format("menus/tools/version_inicio_listo"_i18n, bien)
                                                               : fmt::format("menus/tools/version_inicio_parcial"_i18n, bien, lista.size());
            ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
        }));
        frame->addStage(new ConfirmPage_Deferred(frame, mensaje));
        brls::Application::pushView(frame);
    });

    brls::ListItem* homeRotosItem = new brls::ListItem("menus/tools/home_rotos"_i18n, "", "menus/tools/home_rotos_sub"_i18n);
    homeRotosItem->getClickEvent()->subscribe([](brls::View* view) {
        const std::vector<homeRotos::Entrada> rotos = homeRotos::buscar();
        if (rotos.empty()) {
            util::showDialogBoxInfo("menus/tools/home_rotos_nada"_i18n);
            return;
        }
        brls::StagedAppletFrame* frame = new brls::StagedAppletFrame();
        frame->setTitle("menus/tools/home_rotos"_i18n);
        frame->addStage(new ConfirmPage(frame, fmt::format("menus/tools/home_rotos_confirm"_i18n, rotos.size(), homeRotos::listado(rotos))));
        auto mensaje = std::make_shared<std::string>();
        frame->addStage(new WorkerPage(frame, "menus/tools/home_rotos_trabajando"_i18n, [mensaje, rotos]() {
            const int quitados = homeRotos::quitar(rotos);
            *mensaje = quitados == static_cast<int>(rotos.size())
                           ? fmt::format("menus/tools/home_rotos_listo"_i18n, quitados)
                           : fmt::format("menus/tools/home_rotos_parcial"_i18n, quitados, rotos.size());
            ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
        }));
        frame->addStage(new ConfirmPage_Deferred(frame, mensaje));
        brls::Application::pushView(frame);
    });
    homeRotosItem->setHeight(LISTITEM_HEIGHT);

    /* Instalacion limpia: el pack con la SD como recien formateada. Antes de
       borrar nada muestra una vista previa y pide mantener A. */
    brls::ListItem* limpia = new brls::ListItem("menus/limpia/entrada"_i18n, "", "menus/limpia/entrada_sub"_i18n);
    {
        const nlohmann::ordered_json links = nxlinks;
        limpia->getClickEvent()->subscribe([links, erista](brls::View* view) { instalacionLimpia::abrir(links, erista); });
    }

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

    /* Crear y gestionar accesos directos en una sola opción: así la tarjeta de
       Juegos y menú HOME no crece. Al abrirla se elige qué hacer. */
    brls::ListItem* accesos = new brls::ListItem("menus/tools/accesos"_i18n, "", "menus/tools/accesos_sub"_i18n);
    accesos->getClickEvent()->subscribe([](brls::View* view) {
        brls::Dialog* dialogo = new brls::Dialog("menus/tools/accesos_pregunta"_i18n);
        dialogo->addButton("menus/tools/accesos_crear"_i18n, [dialogo](brls::View*) {
            dialogo->close([]() { brls::Application::pushView(new ForwarderCreatePage()); });
        });
        dialogo->addButton("menus/tools/accesos_gestionar"_i18n, [dialogo](brls::View*) {
            dialogo->close([]() { brls::Application::pushView(new ForwarderManagePage()); });
        });
        dialogo->setCancelable(true);
        dialogo->open();
    });

    /* Tema de colores (reemplaza a "Pestanas ocultas", que con el rediseno ya no
       servia). Se aplica al instante y queda guardado: la pantalla de carga del
       proximo arranque ya sale con su color. */
    brls::ListItem* tema = new brls::ListItem("menus/tools/tema"_i18n, "", "menus/tools/tema_sub"_i18n);
    tema->setValue(oqb::nombreTema(oqb::temaActual()), false, false);
    tema->setHeight(LISTITEM_HEIGHT);
    tema->getClickEvent()->subscribe([tema](brls::View* view) {
        brls::List* l = new brls::List();
        for (int i = 0; i < oqb::cantidadTemas(); ++i) {
            brls::ListItem* opcion = new brls::ListItem(oqb::nombreTema(i));
            if (i == oqb::temaActual())
                opcion->setValue("menus/tools/tema_actual"_i18n, false, false);
            opcion->getClickEvent()->subscribe([tema, i](brls::View* v) {
                oqb::elegirTema(i);
                tema->setValue(oqb::nombreTema(i), false, false);
                brls::Application::popView();
            });
            l->addView(opcion);
        }
        brls::AppletFrame* frame = new brls::AppletFrame(true, true);
        frame->setContentView(l);
        brls::PopupFrame::open("menus/tools/tema"_i18n, frame, "menus/tools/tema_sub"_i18n, "");
    });

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

    /* Cada grupo es una tarjeta y crece con cada opción. Como mucho 4: si hay
       más, se ven 3 y «Ver más» abre el resto en una ventana. */
    const auto grupo = [this](const std::string& titulo, const std::vector<brls::View*>& entradas) {
        if (entradas.empty())
            return;

        brls::Header* cabecera = new HeaderOqb(titulo);
        cabecera->setHeight(34);
        this->addView(cabecera);

        constexpr size_t MAXIMO = 4;
        std::vector<brls::ListItem*> resto;
        for (size_t i = 0; i < entradas.size(); ++i) {
            auto* item = dynamic_cast<brls::ListItem*>(entradas[i]);
            if (entradas.size() > MAXIMO && i >= MAXIMO - 1 && item)
                resto.push_back(item);
            else
                this->addView(entradas[i]);
        }
        if (resto.empty())
            return;
        this->sueltas.insert(this->sueltas.end(), resto.begin(), resto.end());
        brls::ListItem* verMas = new brls::ListItem(fmt::format("menus/tools/ver_mas"_i18n, resto.size()));
        verMas->getClickEvent()->subscribe([titulo, resto](brls::View* view) {
            brls::List* lista = new brls::List();
            for (brls::ListItem* original : resto) {
                brls::ListItem* copia = new brls::ListItem(original->getLabel(), "", original->getSubLabel());
                // Se cierra la ventana y se abre la opción de verdad, como si se hubiera elegido en la tarjeta.
                copia->getClickEvent()->subscribe([original](brls::View*) {
                    brls::Application::popView(brls::ViewAnimation::FADE, [original]() { original->onClick(); });
                });
                lista->addView(copia);
            }
            brls::AppletFrame* frame = new brls::AppletFrame(true, true);
            frame->setContentView(lista);
            brls::PopupFrame::open(titulo, frame, "", "");
        });
        this->addView(verMas);
    };

    const auto siVisible = [&visible](std::vector<brls::View*>& destino, const char* clave, brls::View* vista) {
        if (visible(clave))
            destino.push_back(vista);
    };

    std::vector<brls::View*> app, juegos, consola, red, archivos, inicio;

    app.push_back(updateApp);  // siempre: sin actualizacion ofrece volver a descargar
    /* El rediseno de la 4.0 es grande: quien prefiera el diseno anterior puede
       volver a la 3.2 (su release sigue publicado). "Actualizar" siempre lleva
       a la ultima. */
    {
        brls::ListItem* anterior = new brls::ListItem("menus/app_update/anterior"_i18n, "", "menus/app_update/anterior_sub"_i18n);
        anterior->setHeight(LISTITEM_HEIGHT);
        anterior->getClickEvent()->subscribe([](brls::View* view) { appUpdate::pushFlowVersion("3.2"); });
        app.push_back(anterior);
    }
    siVisible(app, "language", language);
    app.push_back(tema);

    siVisible(juegos, "cheats", cheats);
    siVisible(juegos, "outdatedtitles", outdatedTitles);
    siVisible(juegos, "limpiaremunand", limpiarEmu);
    siVisible(juegos, "versioninicio", versionItem);

    siVisible(consola, "protection", protectionCheck);
    if (erista) siVisible(consola, "rebootpayload", rebootPayload);
    siVisible(consola, "jccolor", JCcolor);
    siVisible(consola, "pccolor", PCcolor);
    {
        // La luz que late al instalar (y destella, parpadea o queda fija según el caso).
        auto* luz = new brls::ToggleListItem("menus/tools/luz_mandos"_i18n, led::activada(), "menus/tools/luz_mandos_sub"_i18n);
        luz->setHeight(LISTITEM_HEIGHT);
        luz->getClickEvent()->subscribe([luz](brls::View*) {
            led::setActivada(luz->getToggleState());
            if (luz->getToggleState()) led::exito();  // para que se vea cómo es
        });
        consola.push_back(luz);
    }

    siVisible(red, "netsettings", netSettings);

    siVisible(archivos, "move", move);
    siVisible(archivos, "cleanup", cleanUp);
    siVisible(archivos, "cleanup", cleanMac);
    siVisible(archivos, "cleanup", homeRotosItem);
    siVisible(archivos, "cleanup", limpia);

    if (visible("createforwarder") || visible("manageforwarders"))
        inicio.push_back(accesos);

    /* Rediseno: primero lo que mas se usa (dejar la SD en orden), la app al
       final. Cada limpieza dice cuanto hay para liberar. */
    {
        long long total = 0;
        for (const char* archivo : {AMS_FILENAME, APP_FILENAME, FIRMWARE_FILENAME, CHEATS_FILENAME, BOOTLOADER_FILENAME, CUSTOM_FILENAME}) {
            const long long t = tamanoArchivo(archivo);
            if (t > 0) total += t;
        }
        if (total > (1 << 20)) cleanUp->setValue(fmt::format("{:.0f} MB", total / 1048576.0), false, false);
    }
    /* Fuera lo que ya esta en otra seccion: la instalacion limpia (en cada pack),
       el menu de trucos (Bacán Shop e Inicio) y verificar protecciones (Inicio >
       Estado). */
    auto quitar = [](std::vector<brls::View*>& v, brls::View* x) { v.erase(std::remove(v.begin(), v.end(), x), v.end()); };
    quitar(archivos, limpia);
    quitar(juegos, cheats);
    quitar(consola, protectionCheck);
    std::vector<brls::View*> sdOrden;
    for (brls::View* v : {static_cast<brls::View*>(cleanUp), static_cast<brls::View*>(cleanMac), static_cast<brls::View*>(homeRotosItem)})
        if (std::find(archivos.begin(), archivos.end(), v) != archivos.end()) sdOrden.push_back(v);
    std::vector<brls::View*> otrosArchivos;
    for (brls::View* v : archivos)
        if (std::find(sdOrden.begin(), sdOrden.end(), v) == sdOrden.end()) otrosArchivos.push_back(v);

    /* Cuatro cajas, juntando lo parecido: la SD y los archivos; juegos y menu
       HOME; consola, mandos e internet; y la app. La hora va en Nextendo, el
       diagnostico en Inicio > Estado y el navegador se quito. */
    sdOrden.insert(sdOrden.end(), otrosArchivos.begin(), otrosArchivos.end());
    juegos.insert(juegos.end(), inicio.begin(), inicio.end());
    consola.insert(consola.end(), red.begin(), red.end());
    grupo("menus/inicio/grupo_sd"_i18n, sdOrden);
    grupo("menus/inicio/grupo_juegos_home"_i18n, juegos);
    grupo("menus/inicio/grupo_consola_red"_i18n, consola);
    grupo("menus/tools/grupo_app"_i18n, app);
}

ToolsTab::~ToolsTab()
{
    for (brls::View* v : this->sueltas)
        delete v;
}
