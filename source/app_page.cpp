#include "app_page.hpp"

#include <switch.h>

#include <filesystem>
#include <fstream>
#include <regex>

#include "confirm_page.hpp"
#include "current_cfw.hpp"
#include "download.hpp"
#include "download_cheats_page.hpp"
#include "extract.hpp"
#include "fs.hpp"
#include "utils.hpp"
#include "worker_page.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

AppPage::AppPage() : AppletFrame(true, true)
{
    list = new brls::List();
}

void AppPage::PopulatePage()
{
    this->CreateLabel();

    NsApplicationRecord* records = new NsApplicationRecord[MaxTitleCount];
    NsApplicationControlData* controlData = NULL;
    std::string name;

    s32 recordCount = 0;
    u64 controlSize = 0;
    u64 tid;

    if (!util::isApplet()) {
        if (R_SUCCEEDED(nsListApplicationRecord(records, MaxTitleCount, 0, &recordCount))) {
            for (s32 i = 0; i < recordCount; i++) {
                controlSize = 0;

                if (R_FAILED(InitControlData(&controlData))) break;

                tid = records[i].application_id;

                if (R_FAILED(GetControlData(tid, controlData, controlSize, name)))
                    continue;

                listItem = new brls::ListItem(name, "", util::formatApplicationId(tid));
                listItem->setThumbnail(controlData->icon, sizeof(controlData->icon));
                this->AddListItem(name, tid);
            }
            free(controlData);
        }
    }
    else {
        tid = GetCurrentApplicationId();
        if (R_SUCCEEDED(InitControlData(&controlData)) && R_SUCCEEDED(GetControlData(tid & 0xFFFFFFFFFFFFF000, controlData, controlSize, name))) {
            listItem = new brls::ListItem(name, "", util::formatApplicationId(tid));
            listItem->setThumbnail(controlData->icon, sizeof(controlData->icon));
            this->AddListItem(name, tid);
        }
        label = new brls::Label(brls::LabelStyle::SMALL, "menus/common/applet_mode_not_supported"_i18n, true);
        list->addView(label);
    }
    delete[] records;

    brls::Logger::debug("count {}", list->getViewsCount());

    if (!list->getViewsCount()) {
        list->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/common/nothing_to_see"_i18n, true));
    }

    this->setContentView(list);
}

void AppPage::CreateDownloadAllButton()
{
    std::string text("menus/cheats/downloading"_i18n);
    std::string url = "";
    switch (CurrentCfw::running_cfw) {
        case CFW::ams:
            url += CHEATS_URL_CONTENTS;
            break;
        case CFW::rnx:
            url += CHEATS_URL_CONTENTS;
            break;
        case CFW::sxos:
            url += CHEATS_URL_CONTENTS;
            break;
    }
    text += url;
    download = new brls::ListItem("menus/cheats/dl_latest"_i18n);
    download->getClickEvent()->subscribe([url, text](brls::View* view) {
        brls::StagedAppletFrame* stagedFrame = new brls::StagedAppletFrame();
        stagedFrame->setTitle("menus/cheats/getting_cheats"_i18n);
        stagedFrame->addStage(
            new ConfirmPage(stagedFrame, text));
        stagedFrame->addStage(
            new WorkerPage(stagedFrame, "menus/common/downloading"_i18n, [url]() { util::downloadArchive(url, contentType::cheats); }));
        stagedFrame->addStage(
            new WorkerPage(stagedFrame, "menus/common/extracting"_i18n, []() { util::extractArchive(contentType::cheats); }));
        stagedFrame->addStage(
            new ConfirmPage_Done(stagedFrame, "menus/common/all_done"_i18n));
        brls::Application::pushView(stagedFrame);
    });
    list->addView(download);
}

u32 AppPage::InitControlData(NsApplicationControlData** controlData)
{
    free(*controlData);
    *controlData = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));
    if (*controlData == NULL) {
        return 300;
    }
    else {
        memset(*controlData, 0, sizeof(NsApplicationControlData));
        return 0;
    }
}

u32 AppPage::GetControlData(u64 tid, NsApplicationControlData* controlData, u64& controlSize, std::string& name)
{
    Result rc;
    NacpLanguageEntry* langEntry = NULL;
    rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, tid, controlData, sizeof(NsApplicationControlData), &controlSize);
    if (R_FAILED(rc)) return rc;

    if (controlSize < sizeof(controlData->nacp)) return 100;

    rc = nacpGetLanguageEntry(&controlData->nacp, &langEntry);
    if (R_FAILED(rc)) return rc;

    if (!langEntry->name) return 200;

    name = langEntry->name;

    return 0;
}

void AppPage::AddListItem(const std::string& name, u64 tid)
{
    list->addView(listItem);
}

uint64_t AppPage::GetCurrentApplicationId()
{
    Result rc = 0;
    uint64_t pid = 0;
    uint64_t tid = 0;

    rc = pmdmntGetApplicationProcessId(&pid);
    if (rc == 0x20f || R_FAILED(rc)) return 0;

    rc = pminfoGetProgramId(&tid, pid);
    if (rc == 0x20f || R_FAILED(rc)) return 0;

    return tid;
}

AppPage_CheatSlips::AppPage_CheatSlips() : AppPage()
{
    this->PopulatePage();
    this->registerAction("menus/cheats/cheatslips_logout"_i18n, brls::Key::X, [this] {
        std::filesystem::remove(TOKEN_PATH);
        return true;
    });
}

void AppPage_CheatSlips::CreateLabel()
{
    this->setTitle("menus/cheats/cheatslips_title"_i18n);
    label = new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/cheats/cheatslips_select"_i18n, true);
    list->addView(label);
}

void AppPage_CheatSlips::AddListItem(const std::string& name, u64 tid)
{
    listItem->getClickEvent()->subscribe([tid, name](brls::View* view) { brls::Application::pushView(new DownloadCheatsPage_CheatSlips(tid, name)); });
    list->addView(listItem);
}

AppPage_Gbatemp::AppPage_Gbatemp() : AppPage()
{
    this->PopulatePage();
    this->setIcon("romfs:/gbatemp_icon.png");
}

void AppPage_Gbatemp::CreateLabel()
{
    this->setTitle("menus/cheats/gbatemp_title"_i18n);
    label = new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/cheats/cheatslips_select"_i18n, true);
    list->addView(label);
}

void AppPage_Gbatemp::AddListItem(const std::string& name, u64 tid)
{
    listItem->getClickEvent()->subscribe([tid, name](brls::View* view) { brls::Application::pushView(new DownloadCheatsPage_Gbatemp(tid, name)); });
    list->addView(listItem);
}

AppPage_Gfx::AppPage_Gfx() : AppPage()
{
    this->PopulatePage();
}

void AppPage_Gfx::CreateLabel()
{
    this->setTitle("menus/cheats/gfx_title"_i18n);
    label = new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/cheats/cheatslips_select"_i18n, true);
    list->addView(label);
}

void AppPage_Gfx::AddListItem(const std::string& name, u64 tid)
{
    listItem->getClickEvent()->subscribe([tid, name](brls::View* view) { brls::Application::pushView(new DownloadCheatsPage_Gfx(tid, name)); });
    list->addView(listItem);
}


AppPage_Exclude::AppPage_Exclude() : AppPage()
{
    this->PopulatePage();
}

void AppPage_Exclude::CreateLabel()
{
    this->setTitle("menus/cheats/exclude_titles"_i18n);
    label = new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/cheats/exclude_titles_desc"_i18n, true);
    list->addView(label);
}

void AppPage_Exclude::PopulatePage()
{
    this->CreateLabel();

    NsApplicationRecord* records = new NsApplicationRecord[MaxTitleCount];
    NsApplicationControlData* controlData = NULL;
    std::string name;

    s32 recordCount = 0;
    u64 controlSize = 0;
    u64 tid;

    auto titles = fs::readLineByLine(CHEATS_EXCLUDE);

    if (!util::isApplet()) {
        if (R_SUCCEEDED(nsListApplicationRecord(records, MaxTitleCount, 0, &recordCount))) {
            for (s32 i = 0; i < recordCount; i++) {
                controlSize = 0;

                if (R_FAILED(InitControlData(&controlData))) break;

                tid = records[i].application_id;

                if (R_FAILED(GetControlData(tid, controlData, controlSize, name)))
                    continue;

                brls::ToggleListItem* listItem;
                listItem = new brls::ToggleListItem(std::string(name), titles.find(util::formatApplicationId(tid)) != titles.end() ? 0 : 1);

                listItem->setThumbnail(controlData->icon, sizeof(controlData->icon));
                items.insert(std::make_pair(listItem, util::formatApplicationId(tid)));
                list->addView(listItem);
            }
            free(controlData);
        }
    }
    else {
        label = new brls::Label(brls::LabelStyle::SMALL, "menus/common/applet_mode_not_supported"_i18n, true);
        list->addView(label);
    }
    delete[] records;

    list->registerAction("menus/cheats/exclude_titles_save"_i18n, brls::Key::B, [this] {
        std::set<std::string> exclude;
        for (const auto& item : items) {
            if (!item.first->getToggleState()) {
                exclude.insert(item.second);
            }
        }
        extract::writeTitlesToFile(exclude, CHEATS_EXCLUDE);
        brls::Application::popView();
        return true;
    });

    this->registerAction("menus/cheats/exclude_all"_i18n, brls::Key::X, [this] {
        std::set<std::string> exclude;
        for (const auto& item : items) {
            exclude.insert(item.second);
        }
        extract::writeTitlesToFile(exclude, CHEATS_EXCLUDE);
        brls::Application::popView();
        return true;
    });

    this->registerAction("menus/cheats/exclude_none"_i18n, brls::Key::Y, [this] {
        extract::writeTitlesToFile({}, CHEATS_EXCLUDE);
        brls::Application::popView();
        return true;
    });

    this->setContentView(list);
}

AppPage_DownloadedCheats::AppPage_DownloadedCheats() : AppPage()
{
    GetExistingCheatsTids();
    this->PopulatePage();
}

void AppPage_DownloadedCheats::CreateLabel()
{
    this->setTitle("menus/cheats/installed"_i18n);
    label = new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/cheats/label"_i18n, true);
    list->addView(label);
}

void AppPage_DownloadedCheats::AddListItem(const std::string& name, u64 tid)
{
    auto tid_str = util::formatApplicationId(tid);
    if (titles.find(tid_str) != titles.end()) {
        listItem->getClickEvent()->subscribe([tid, name](brls::View* view) { cheats_util::ShowCheatFiles(tid, name); });
        listItem->registerAction("menus/cheats/delete_cheats"_i18n, brls::Key::Y, [tid_str] {
            util::showDialogBoxInfo(extract::removeCheatsDirectory(fmt::format("{}{}", util::getContentsPath(), tid_str)) ? "menus/common/all_done"_i18n : fmt::format("menus/cheats/deletion_error"_i18n, tid_str));
            return true;
        });
        list->addView(listItem);
    }
}

void AppPage_DownloadedCheats::GetExistingCheatsTids()
{
    std::string path = util::getContentsPath();
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        std::string cheatsPath = entry.path().string() + "/cheats";
        if (std::filesystem::exists(cheatsPath) && !std::filesystem::is_empty(cheatsPath)) {
            for (const auto& cheatFile : std::filesystem::directory_iterator(cheatsPath)) {
                if (extract::isBID(cheatFile.path().filename().stem())) {
                    titles.insert(util::upperCase(cheatsPath.substr(cheatsPath.length() - 7 - 16, 16)));
                    break;
                }
            }
        }
    }
}

AppPage_OutdatedTitles::AppPage_OutdatedTitles() : AppPage()
{
    download::getRequest(LOOKUP_TABLE_URL, versions);
    if (versions.empty()) {
        list->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/main/links_not_found"_i18n, true));
        this->setContentView(list);
    }
    else {
        this->PopulatePage();
    }
}

void AppPage_OutdatedTitles::PopulatePage()
{
    this->progressLabel = new brls::Label(brls::LabelStyle::DESCRIPTION, "", true);
    list->addView(this->progressLabel);

    if (util::isApplet()) {
        this->progressLabel->setText("menus/common/applet_mode_not_supported"_i18n);
        this->setContentView(list);
        return;
    }

    NsApplicationRecord* records = new NsApplicationRecord[MaxTitleCount];
    s32 recordCount = 0;

    if (R_SUCCEEDED(nsListApplicationRecord(records, MaxTitleCount, 0, &recordCount))) {
        this->pending.reserve(recordCount);
        for (s32 i = 0; i < recordCount; i++) {
            this->pending.push_back(records[i].application_id);
        }
    }
    delete[] records;

    this->scanning = !this->pending.empty();
    this->progressLabel->setText(this->scanning
                                     ? fmt::format("menus/tools/scanning"_i18n, 0, this->pending.size())
                                     : "menus/common/nothing_to_see"_i18n);

    this->setContentView(list);
}

void AppPage_OutdatedTitles::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    if (this->scanning) {
        // Pocos por frame: así la interfaz sigue respondiendo y se puede salir
        // con B en el medio si uno se arrepiente.
        constexpr size_t BATCH = 4;
        const size_t end = std::min(this->scanned + BATCH, this->pending.size());

        for (; this->scanned < end; this->scanned++) {
            const u64 tid = this->pending[this->scanned];
            const std::string tid_string = util::formatApplicationId(tid);

            const auto entry = this->versions.find(tid_string);
            if (entry == this->versions.end()) continue;

            const u32 latest = entry->at("latest");
            const u32 local = cheats_util::GetVersion(tid);
            if (local >= latest) continue;

            // El control data (que arrastra el icono) se pide sólo para los
            // títulos que de verdad tienen actualización, no para todos.
            NsApplicationControlData* controlData = NULL;
            if (R_FAILED(InitControlData(&controlData))) continue;

            u64 controlSize = 0;
            std::string name;
            if (R_SUCCEEDED(GetControlData(tid, controlData, controlSize, name))) {
                brls::ListItem* item = new brls::ListItem(
                    name, "", fmt::format("{}\t|\t v{} (local) → v{} (latest)", tid_string, local, latest));
                item->setThumbnail(controlData->icon, sizeof(controlData->icon));
                list->addView(item);
                this->found++;
            }

            free(controlData);
        }

        if (this->scanned >= this->pending.size()) {
            this->scanning = false;
            this->progressLabel->setText(this->found ? "menus/tools/outdated_titles_desc"_i18n
                                                     : "menus/common/nothing_to_see"_i18n);
        }
        else {
            this->progressLabel->setText(fmt::format("menus/tools/scanning"_i18n, this->scanned, this->pending.size()));
        }

        this->invalidate();
    }

    brls::AppletFrame::draw(vg, x, y, width, height, style, ctx);
}