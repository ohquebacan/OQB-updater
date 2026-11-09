#include "hora.hpp"

#include <borealis.hpp>
#include <switch.h>

#include <memory>

#include "confirm_page.hpp"
#include "ntp.hpp"
#include "progress_event.hpp"
#include "protection.hpp"
#include "utils.hpp"
#include "worker_page.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace hora {

    void sincronizar()
    {
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
    
    }

    bool leerAutomatica(bool& activa)
    {
        activa = false;
        return R_SUCCEEDED(setsysIsUserSystemClockAutomaticCorrectionEnabled(&activa));
    }

    void alternarAutomatica()
    {
        bool autoCorrection = false;
        const bool autoCorrectionRead = leerAutomatica(autoCorrection);

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
        if (turningOn && !protection::dnsBlockingOk(protection::runOnConsole())) {
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
    
    }

}  // namespace hora
