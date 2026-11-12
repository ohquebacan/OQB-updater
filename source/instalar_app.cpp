#include "instalar_app.hpp"

#include <borealis.hpp>
#include <memory>

#include "app_updates.hpp"
#include "confirm_page.hpp"
#include "download.hpp"
#include "extract.hpp"
#include "fs.hpp"
#include "list_download_tab.hpp"
#include "nsp_installer.hpp"
#include "utils.hpp"
#include "worker_page.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

void instalarAppDelCatalogo(const nlohmann::ordered_json& nxlinks, const std::string& nombre, const std::string& url)
{
    const std::string fecha = appUpdates::published(nxlinks, url);
    brls::StagedAppletFrame* frame = new brls::StagedAppletFrame();
    frame->setTitle(nombre);
    frame->addStage(new ConfirmPage(frame, "menus/common/download"_i18n + nombre));

    // Donde queda el .nro, para ofrecer el acceso directo del menu HOME. En un
    // zip lo descubre el worker antes de extraer.
    auto nroPath = std::make_shared<std::string>();

    if (url.size() > 4 && url.substr(url.size() - 4) == ".nro") {
        const std::string destino = util::resolveNroDestination(url.substr(url.rfind('/') + 1));
        *nroPath = destino;
        frame->addStage(new WorkerPage(frame, "menus/common/downloading"_i18n, [url, destino, fecha]() {
            fs::createTree(destino.substr(0, destino.rfind('/') + 1));
            // Solo si bajo de verdad: un error deja el archivo viejo en su lugar.
            if (download::downloadFile(url, destino, OFF) == 200)
                appUpdates::recordInstalled(url, fecha);
        }));
    }
    else if (nspInstaller::isNspUrl(url)) {
        const std::string destino = "/config/aio-switch-updater/download.nsp";
        frame->addStage(new WorkerPage(frame, "menus/common/downloading"_i18n, [url, destino]() {
            download::downloadFile(url, destino, OFF);
        }));
        frame->addStage(new WorkerPage(frame, "menus/common/instalando_nsp"_i18n, [url, destino, fecha]() {
            if (R_SUCCEEDED(nspInstaller::installAndDelete(destino)))
                appUpdates::recordInstalled(url, fecha);
        }));
    }
    else {
        // Un zip puede traer homebrew, sysmodules y tambien uno o varios NSP.
        frame->addStage(new WorkerPage(frame, "menus/common/downloading"_i18n, [url]() { util::downloadArchive(url, contentType::apps); }));
        frame->addStage(new WorkerPage(frame, "menus/common/extracting"_i18n, [nroPath, url, fecha]() {
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
        }));
    }

    // La misma pregunta que la lista de apps; sin .nro (un sysmodule) se saltea sola.
    ListDownloadTab::addForwarderStages(frame, nroPath);
    brls::Application::pushView(frame);
}
