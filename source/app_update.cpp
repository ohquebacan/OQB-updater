#include "app_update.hpp"

#include <borealis.hpp>

#include "confirm_page.hpp"
#include "constants.hpp"
#include "extract.hpp"
#include "utils.hpp"
#include "worker_page.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace appUpdate {

    namespace {
        constexpr const char AppVersion[] = APP_VERSION;

        // El aviso es por arranque, no por vista: entrar y salir de las
        // pestanas no tiene que volver a sacarlo.
        bool noticeShown = false;
    }  // namespace

    bool isAvailable(const std::string& tag)
    {
        return !tag.empty() && tag != AppVersion;
    }

    void pushFlow(const std::string& targetTag, bool hasUpdate)
    {
        brls::StagedAppletFrame* stagedFrame = new brls::StagedAppletFrame();
        stagedFrame->setTitle(hasUpdate
                                  ? fmt::format("menus/app_update/staged_title"_i18n, targetTag)
                                  : fmt::format("menus/app_update/staged_title_again"_i18n, targetTag));
        stagedFrame->addStage(new ConfirmPage(stagedFrame,
                                              hasUpdate
                                                  ? fmt::format("menus/app_update/confirm"_i18n, targetTag)
                                                  : fmt::format("menus/app_update/confirm_again"_i18n, targetTag)));
        stagedFrame->addStage(new WorkerPage(stagedFrame, "menus/common/downloading"_i18n,
                                             []() {
                                                 util::downloadArchive(APP_URL, contentType::app);
                                             }));
        stagedFrame->addStage(new WorkerPage(stagedFrame, "menus/common/extracting"_i18n,
                                             []() {
                                                 // Extrae el NRO a /config/aio-switch-updater/; el
                                                 // forwarder lo mueve a su sitio y lanza el nuevo binario.
                                                 util::extractArchive(contentType::app);
                                             }));
        stagedFrame->addStage(new ConfirmPage_AppUpdate(stagedFrame, "menus/common/all_done"_i18n));
        brls::Application::pushView(stagedFrame);
    }

    void showNoticeIfAvailable(const std::string& tag)
    {
        if (noticeShown || !isAvailable(tag))
            return;
        noticeShown = true;

        brls::Dialog* dialog = new brls::Dialog(
            fmt::format("menus/app_update/notice"_i18n, AppVersion, tag));

        // El dialogo no se cierra solo al pulsar un boton: el callback va
        // suscrito directo al click y cerrar es cosa suya. Y el flujo se abre
        // desde el callback de close(), ya terminada la animacion, para no
        // empujar una vista mientras se esta sacando otra.
        dialog->addButton("menus/app_update/notice_now"_i18n, [dialog, tag](brls::View* view) {
            dialog->close([tag]() { pushFlow(tag, true); });
        });
        dialog->addButton("menus/app_update/notice_later"_i18n, [dialog](brls::View* view) {
            dialog->close();
        });

        // Sin salida a lo bruto: que se elija una de las dos, para que el aviso
        // no se cierre sin querer y pase desapercibido.
        dialog->setCancelable(false);
        dialog->open();
    }

}  // namespace appUpdate
