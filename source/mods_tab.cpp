#include "mods_tab.hpp"

#include <switch.h>
#include <filesystem>
#include <regex>

#include "confirm_page.hpp"
#include "constants.hpp"
#include "download.hpp"
#include "extract.hpp"
#include "fs.hpp"
#include "utils.hpp"
#include "worker_page.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

constexpr int MaxTitleCount = 64000;
constexpr const char GB_MOD_FILENAME[] = "/config/aio-switch-updater/gb_mod.zip";

static std::string sanitizePath(const std::string& str)
{
    return std::regex_replace(str, std::regex("[:/\\\\<>|*?\"]+"), "-");
}

static std::string formatFileSize(int bytes)
{
    if (bytes < 1024 * 1024)
        return fmt::format("{:.1f} KB", bytes / 1024.0);
    return fmt::format("{:.1f} MB", bytes / (1024.0 * 1024.0));
}

// ModListPage

ModListPage::ModListPage(const GBGame& game, int page, const std::string& search)
    : AppletFrame(true, true), game(game), page(page), search(search)
{
    list = new brls::List();
    this->setTitle(fmt::format("Mods: {}", game.getTitle()));

    modList = GBModList(game, page, search);

    this->registerAction("< Anterior", brls::Key::L, [this] {
        if (this->page > 1)
            brls::Application::pushView(new ModListPage(this->game, this->page - 1, this->search));
        return true;
    });

    this->registerAction("Siguiente >", brls::Key::R, [this] {
        brls::Application::pushView(new ModListPage(this->game, this->page + 1, this->search));
        return true;
    });

    this->registerAction("Buscar", brls::Key::Y, [this] {
        std::string term;
        brls::Swkbd::openForText([&term](std::string text) { term = text; },
            "Buscar mods", "", 64, "", 0, "Buscar", "");
        if (term.size() >= 3)
            brls::Application::pushView(new ModListPage(this->game, 1, term));
        return true;
    });

    buildList();
    this->setContentView(list);
}

void ModListPage::buildList()
{
    if (modList.isEmpty()) {
        list->addView(new brls::Label(brls::LabelStyle::DESCRIPTION,
            fmt::format("No se encontraron mods (página {}).", page), true));
        return;
    }

    std::string hint = search.empty()
        ? fmt::format("Página {} — L/R para navegar, Y para buscar", page)
        : fmt::format("Búsqueda: \"{}\" — página {}", search, page);
    list->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, hint, true));

    for (auto& mod : modList.getMods()) {
        brls::ListItem* item = new brls::ListItem(
            mod.getName(),
            fmt::format("Autor: {}", mod.getAuthor()));
        item->setHeight(70);

        item->getClickEvent()->subscribe([mod](brls::View* view) mutable {
            mod.loadMod();

            if (mod.getFiles().empty()) {
                util::showDialogBoxInfo("No se encontraron archivos para este mod.");
                return true;
            }

            brls::AppletFrame* detailFrame = new brls::AppletFrame(true, true);
            detailFrame->setTitle(mod.getName());
            brls::List* detailList = new brls::List();

            if (!mod.getDescription().empty()) {
                std::string desc = mod.getDescription();
                if (desc.size() > 400)
                    desc = desc.substr(0, 400) + "...";
                detailList->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, desc, true));
            }

            detailList->addView(new brls::Label(brls::LabelStyle::SMALL,
                fmt::format("Archivos ({}): presiona A para descargar", mod.getFiles().size()), true));

            for (auto file : mod.getFiles()) {
                std::string fileLabel = fmt::format("{} ({})", file.getName(), formatFileSize(file.getSize()));
                brls::ListItem* fileItem = new brls::ListItem(fileLabel);
                fileItem->setHeight(60);

                fileItem->getClickEvent()->subscribe([file](brls::View* view) mutable {
                    file.loadFile();

                    std::string gameTid = file.getGame().getTid();
                    std::string gameName = sanitizePath(file.getGame().getTitle());
                    std::string modName = sanitizePath(file.getModName());
                    std::string destPath;

                    if (gameTid == "01006A800016E000")
                        destPath = fmt::format("/ultimate/mods/{}/", modName);
                    else if (file.getRomfs())
                        destPath = fmt::format("/atmosphere/contents/{}/", gameTid);
                    else
                        destPath = fmt::format("/mods/{}/{}/", gameName, modName);

                    brls::StagedAppletFrame* stagedFrame = new brls::StagedAppletFrame();
                    stagedFrame->setTitle(fmt::format("Mod: {}", file.getName()));

                    std::string confirmText = fmt::format(
                        "Descargar {} ({}) y extraer en:\n{}",
                        file.getName(), formatFileSize(file.getSize()), destPath);

                    stagedFrame->addStage(new ConfirmPage(stagedFrame, confirmText));
                    stagedFrame->addStage(new WorkerPage(stagedFrame, "menus/common/downloading"_i18n,
                        [file]() mutable {
                            download::downloadFile(file.getUrl(), GB_MOD_FILENAME);
                        }));
                    stagedFrame->addStage(new WorkerPage(stagedFrame, "menus/common/extracting"_i18n,
                        [destPath]() {
                            fs::createTree(destPath);
                            extract::extract(GB_MOD_FILENAME, destPath);
                        }));
                    stagedFrame->addStage(new ConfirmPage_Done(stagedFrame, "menus/common/all_done"_i18n));

                    brls::Application::pushView(stagedFrame);
                    return true;
                });

                detailList->addView(fileItem);
            }

            detailFrame->setContentView(detailList);
            brls::Application::pushView(detailFrame);
            return true;
        });

        list->addView(item);
    }
}

// ModsTab

ModsTab::ModsTab() : brls::List()
{
    this->addView(new brls::Label(brls::LabelStyle::DESCRIPTION,
        "Descarga mods para tus juegos desde GameBanana.", true));

    if (util::isApplet()) {
        this->addView(new brls::Label(brls::LabelStyle::SMALL,
            "menus/common/applet_mode_not_supported"_i18n, true));
        return;
    }

    NsApplicationRecord* records = new NsApplicationRecord[MaxTitleCount];
    NsApplicationControlData* controlData = nullptr;
    s32 recordCount = 0;
    u64 controlSize = 0;
    bool anyAdded = false;

    if (R_SUCCEEDED(nsListApplicationRecord(records, MaxTitleCount, 0, &recordCount))) {
        for (s32 i = 0; i < recordCount; i++) {
            free(controlData);
            controlData = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));
            if (!controlData) break;
            memset(controlData, 0, sizeof(NsApplicationControlData));

            u64 tid = records[i].application_id;
            Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, tid,
                controlData, sizeof(NsApplicationControlData), &controlSize);
            if (R_FAILED(rc)) continue;
            if (controlSize < sizeof(controlData->nacp)) continue;

            NacpLanguageEntry* langEntry = nullptr;
            if (R_FAILED(nacpGetLanguageEntry(&controlData->nacp, &langEntry))) continue;
            if (!langEntry || !langEntry->name[0]) continue;

            std::string name = langEntry->name;
            std::string tidStr = util::formatApplicationId(tid);

            brls::ListItem* item = new brls::ListItem(name, tidStr);
            item->setHeight(70);
            item->setThumbnail(controlData->icon, sizeof(controlData->icon));

            item->getClickEvent()->subscribe([name, tidStr](brls::View* view) {
                GBGame game(name, tidStr);
                if (game.hasError()) {
                    util::showDialogBoxInfo("Error de conexión con GameBanana.");
                    return true;
                }
                if (!game.isValid()) {
                    util::showDialogBoxInfo(fmt::format("\"{}\" no encontrado en GameBanana.", name));
                    return true;
                }
                brls::Application::pushView(new ModListPage(game));
                return true;
            });

            this->addView(item);
            anyAdded = true;
        }
        free(controlData);
    }

    delete[] records;

    if (!anyAdded) {
        this->addView(new brls::Label(brls::LabelStyle::DESCRIPTION,
            "No se encontraron juegos instalados.", true));
    }
}
