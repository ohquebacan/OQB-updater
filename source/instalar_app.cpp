#include "instalar_app.hpp"

#include <borealis.hpp>
#include <memory>

#include "app_updates.hpp"
#include "cola.hpp"
#include "confirm_page.hpp"
#include "current_cfw.hpp"
#include "dialogo_centrado.hpp"
#include "download.hpp"
#include "extract.hpp"
#include "fs.hpp"
#include "list_download_tab.hpp"
#include "nsp_installer.hpp"
#include "utils.hpp"
#include "worker_page.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

bool bloqueadoPorNexus(const std::string& url)
{
    if (url.find("/sys-icon") == std::string::npos || !CurrentCfw::nexusCorriendo()) return false;
    brls::Dialog* d = new DialogoCentrado("menus/apps/sysicon_nexus"_i18n, 360);
    d->addButton("menus/common/ok"_i18n, [d](brls::View*) { d->close(); });
    d->setCancelable(true);
    d->open();
    return true;
}

void instalarAppDelCatalogo(const nlohmann::ordered_json& nxlinks, const std::string& nombre, const std::string& url, const std::string& nota)
{
    if (bloqueadoPorNexus(url)) return;
    const std::string fecha = appUpdates::published(nxlinks, url);

    /* A la cola de descargas (5.0.0): se sigue usando la app mientras baja.
       El acceso directo se decide al agregarla. */
    const auto agregar = [url, fecha, nombre](bool acceso) {
        // Donde queda el .nro; en un zip lo descubre el paso de extraer.
        auto nroPath = std::make_shared<std::string>();
        std::vector<cola::Paso> pasos;
        if (url.size() > 4 && url.substr(url.size() - 4) == ".nro") {
            const std::string destino = util::resolveNroDestination(url.substr(url.rfind('/') + 1));
            *nroPath = destino;
            pasos.push_back({"menus/common/downloading"_i18n, [url, destino, fecha]() {
                fs::createTree(destino.substr(0, destino.rfind('/') + 1));
                // Solo si bajo de verdad: un error deja el archivo viejo en su lugar.
                if (download::downloadFile(url, destino, OFF) == 200)
                    appUpdates::recordInstalled(url, fecha);
            }});
        }
        else if (nspInstaller::isNspUrl(url)) {
            const std::string destino = "/config/aio-switch-updater/download.nsp";
            pasos.push_back({"menus/common/downloading"_i18n, [url, destino]() { download::downloadFile(url, destino, OFF); }});
            pasos.push_back({"menus/common/instalando_nsp"_i18n, [url, destino, fecha]() {
                if (R_SUCCEEDED(nspInstaller::installAndDelete(destino)))
                    appUpdates::recordInstalled(url, fecha);
            }});
        }
        else {
            // Un zip puede traer homebrew, sysmodules y tambien uno o varios NSP.
            pasos.push_back({"menus/common/downloading"_i18n, [url]() { util::downloadArchive(url, contentType::apps); }});
            pasos.push_back({"menus/common/extracting"_i18n, [nroPath, url, fecha]() {
                const std::string wrapper = extract::detectWrapperDir(CUSTOM_FILENAME);
                *nroPath = extract::findNroInArchive(CUSTOM_FILENAME, ROOT_PATH, wrapper);
                const auto nsps = nspInstaller::findInArchive(CUSTOM_FILENAME, wrapper);
                util::extractArchive(contentType::apps);
                Result rc = 0;
                for (const auto& nsp : nsps) {
                    rc = nspInstaller::installAndDelete(nsp);
                    if (R_FAILED(rc)) break;
                }
                if (R_SUCCEEDED(rc)) appUpdates::recordInstalled(url, fecha);
            }});
        }
        if (acceso) {
            pasos.push_back({"menus/apps/forwarder_creating"_i18n, [nroPath]() {
                if (!nroPath->empty()) ListDownloadTab::crearAcceso(*nroPath);
            }});
        }
        cola::agregar(nombre, std::move(pasos));
        brls::Application::notify(fmt::format("menus/cola/agregado"_i18n, nombre));
    };

    const std::string conNota = nota.empty() ? "" : "\n\n" + nota;
    if (!appUpdates::traeNro(nxlinks, url)) {
        if (nota.empty()) {
            agregar(false);
            return;
        }
        brls::Dialog* d = new brls::Dialog(fmt::format("menus/cola/pregunta_instalar"_i18n, nombre) + conNota);
        d->addButton("menus/common/cancel"_i18n, [d](brls::View*) { d->close(); });
        d->addButton("menus/cola/instalar"_i18n, [d, agregar](brls::View*) { d->close([agregar] { agregar(false); }); });
        d->setCancelable(true);
        d->open();
        return;
    }
    brls::Dialog* d = new brls::Dialog(fmt::format("menus/cola/pregunta_acceso"_i18n, nombre) + conNota);
    d->addButton("menus/common/no"_i18n, [d, agregar](brls::View*) { d->close([agregar] { agregar(false); }); });
    d->addButton("menus/common/yes"_i18n, [d, agregar](brls::View*) { d->close([agregar] { agregar(true); }); });
    d->setCancelable(true);
    d->open();
}

void abrirOInstalar(const nlohmann::ordered_json& nxlinks, const std::string& nombre, const std::string& url, const std::string& argumentos)
{
    const bool alDia = appUpdates::isInstalled(nxlinks, url) && !appUpdates::hasUpdate(nxlinks, appUpdates::installed(), url);
    const std::string ruta = alDia ? appUpdates::rutaParaAbrir(nxlinks, url) : "";
    if (!ruta.empty())
        util::abrirAppInstalada(ruta, nombre, argumentos);
    else
        instalarAppDelCatalogo(nxlinks, nombre, url);
}
