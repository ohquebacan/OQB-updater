#include "forwarder_page.hpp"

#include <switch.h>

#include <algorithm>
#include <filesystem>

#include "confirm_page.hpp"
#include "constants.hpp"
#include "forwarder.hpp"
#include "progress_event.hpp"
#include "utils.hpp"
#include "worker_page.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {

    // Los forwarders que crea esta app usan el prefijo 0x05, fuera del rango
    // de títulos retail. Sirve para reconocerlos entre todos los instalados.
    constexpr u64 FORWARDER_TID_PREFIX = 0x0500000000000000;
    constexpr u32 MaxTitleCount = 64000;

    bool isForwarderTid(u64 tid)
    {
        return (tid & 0xFF00000000000000) == FORWARDER_TID_PREFIX;
    }

    // NROs en /switch/, incluyendo /switch/<app>/<app>.nro, que es la otra
    // convención que entiende hbmenu.
    std::vector<std::string> findNros()
    {
        std::vector<std::string> out;
        std::error_code ec;

        if (!std::filesystem::exists(APP_PATH, ec)) {
            return out;
        }

        for (const auto& entry : std::filesystem::directory_iterator(APP_PATH, ec)) {
            if (ec) break;

            if (entry.is_regular_file(ec) && entry.path().extension() == ".nro") {
                out.push_back(entry.path().string());
            }
            else if (entry.is_directory(ec)) {
                std::error_code subEc;
                for (const auto& sub : std::filesystem::directory_iterator(entry.path(), subEc)) {
                    if (subEc) break;
                    if (sub.is_regular_file(subEc) && sub.path().extension() == ".nro") {
                        out.push_back(sub.path().string());
                    }
                }
            }
        }

        std::sort(out.begin(), out.end());
        return out;
    }

    void pushInstallFlow(const std::string& nroPath, const std::string& name)
    {
        brls::StagedAppletFrame* stagedFrame = new brls::StagedAppletFrame();
        stagedFrame->setTitle("menus/forwarders/create_title"_i18n);
        stagedFrame->addStage(new ConfirmPage(stagedFrame, fmt::format("menus/forwarders/confirm_create"_i18n, name)));

        auto result = std::make_shared<std::string>();
        stagedFrame->addStage(new WorkerPage(stagedFrame, "menus/apps/forwarder_creating"_i18n, [nroPath, result]() {
            auto& progress = ProgressEvent::instance();

            fwd::Config config;
            Result rc = fwd::configFromNro(nroPath, config);
            if (R_SUCCEEDED(rc)) {
                rc = fwd::install(config, [](int step, int total) {
                    ProgressEvent::instance().setTotalSteps(total);
                    ProgressEvent::instance().setStep(step);
                });
            }
            *result = R_SUCCEEDED(rc)
                          ? "menus/common/all_done"_i18n
                          : fmt::format("menus/apps/forwarder_error"_i18n, fwd::resultToString(rc));

            progress.setStep(progress.getMax());
        }));
        stagedFrame->addStage(new ConfirmPage_Deferred(stagedFrame, result));

        brls::Application::pushView(stagedFrame);
    }

}  // namespace

ForwarderCreatePage::ForwarderCreatePage() : AppletFrame(true, true)
{
    this->list = new brls::List();
    this->populate();
}

void ForwarderCreatePage::populate()
{
    this->setTitle("menus/forwarders/create_title"_i18n);
    this->list->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/forwarders/create_desc"_i18n, true));

    const auto nros = findNros();

    // Una sola sesión de ns para todas las consultas de la lista.
    const bool nsReady = R_SUCCEEDED(nsInitialize());

    for (const auto& nroPath : nros) {
        const std::string name = fwd::nameFromNro(nroPath);

        brls::ListItem* item = new brls::ListItem(name, "", nroPath);
        item->setHeight(LISTITEM_HEIGHT);
        if (nsReady && fwd::exists(nroPath)) {
            item->setValue("menus/forwarders/already_created"_i18n, true);
        }
        item->getClickEvent()->subscribe([nroPath, name](brls::View* view) {
            pushInstallFlow(nroPath, name);
        });
        this->list->addView(item);
    }

    if (nsReady) {
        nsExit();
    }

    if (nros.empty()) {
        this->list->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/common/nothing_to_see"_i18n, true));
    }

    this->setContentView(this->list);
}

ForwarderManagePage::ForwarderManagePage() : AppletFrame(true, true)
{
    this->list = new brls::List();
    this->populate();
}

void ForwarderManagePage::populate()
{
    this->setTitle("menus/forwarders/manage_title"_i18n);
    this->list->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/forwarders/manage_desc"_i18n, true));

    int found = 0;

    if (util::isApplet()) {
        this->list->addView(new brls::Label(brls::LabelStyle::SMALL, "menus/common/applet_mode_not_supported"_i18n, true));
    }
    else {
        NsApplicationRecord* records = new NsApplicationRecord[MaxTitleCount];
        s32 recordCount = 0;

        if (R_SUCCEEDED(nsInitialize())) {
            if (R_SUCCEEDED(nsListApplicationRecord(records, MaxTitleCount, 0, &recordCount))) {
                auto controlData = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));

                for (s32 i = 0; i < recordCount; i++) {
                    const u64 tid = records[i].application_id;
                    if (!isForwarderTid(tid)) continue;

                    std::string name = util::formatApplicationId(tid);
                    bool hasIcon = false;

                    if (controlData) {
                        memset(controlData, 0, sizeof(NsApplicationControlData));
                        u64 controlSize = 0;
                        NacpLanguageEntry* langEntry = NULL;
                        if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, tid, controlData, sizeof(NsApplicationControlData), &controlSize)) &&
                            controlSize >= sizeof(controlData->nacp) &&
                            R_SUCCEEDED(nacpGetLanguageEntry(&controlData->nacp, &langEntry)) &&
                            langEntry && langEntry->name[0]) {
                            name = langEntry->name;
                            hasIcon = true;
                        }
                    }

                    brls::ListItem* item = new brls::ListItem(name, "", util::formatApplicationId(tid));
                    item->setHeight(LISTITEM_HEIGHT);
                    if (hasIcon) {
                        item->setThumbnail(controlData->icon, sizeof(controlData->icon));
                    }
                    item->getClickEvent()->subscribe([this, tid, name](brls::View* view) {
                        this->removeForwarder(tid, name);
                    });
                    this->list->addView(item);
                    found++;
                }

                free(controlData);
            }
            nsExit();
        }

        delete[] records;
    }

    if (!found) {
        this->list->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/common/nothing_to_see"_i18n, true));
    }

    this->setContentView(this->list);
}

void ForwarderManagePage::removeForwarder(u64 tid, const std::string& name)
{
    // showDialogBoxBlocking hace busy-wait esperando la respuesta, así que no
    // sirve acá: esto corre en el hilo de la UI y se trabaría a sí mismo.
    brls::Dialog* dialog = new brls::Dialog(fmt::format("menus/forwarders/confirm_delete"_i18n, name));

    dialog->addButton("menus/common/yes"_i18n, [dialog, tid](brls::View* view) {
        Result rc = nsInitialize();
        if (R_SUCCEEDED(rc)) {
            rc = nsDeleteApplicationCompletely(tid);
            nsExit();
        }
        dialog->close();
        util::showDialogBoxInfo(R_SUCCEEDED(rc)
                                    ? "menus/common/all_done"_i18n
                                    : fmt::format("menus/forwarders/delete_error"_i18n, fwd::resultToString(rc)));
        // La lista queda desactualizada; se vuelve a armar al reentrar.
        brls::Application::popView();
    });
    dialog->addButton("menus/common/no"_i18n, [dialog](brls::View* view) { dialog->close(); });
    dialog->setCancelable(true);
    dialog->open();
}
