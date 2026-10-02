#include "list_download_tab.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include "app_page.hpp"
#include "confirm_page.hpp"
#include "current_cfw.hpp"
#include "dialogue_page.hpp"
#include "download.hpp"
#include "extract.hpp"
#include "forwarder.hpp"
#include "fs.hpp"
#include "progress_event.hpp"
#include "utils.hpp"
#include "worker_page.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

ListDownloadTab::ListDownloadTab(const contentType type, const nlohmann::ordered_json& nxlinks) : brls::List(), type(type), nxlinks(nxlinks)
{
    this->setDescription();

    this->createList();

    if (this->type == contentType::cheats) {
        brls::Label* cheatsLabel = new brls::Label(
            brls::LabelStyle::DESCRIPTION,
            "menus/cheats/cheats_label"_i18n,
            true);
        this->addView(cheatsLabel);
        this->createGbatempItem();
        this->createGfxItem();
        this->createCheatSlipItem();
    }

    if (this->type == contentType::bootloaders) {
        this->setDescription(contentType::hekate_ipl);
        this->createList(contentType::hekate_ipl);
        this->setDescription(contentType::payloads);
        this->createList(contentType::payloads);
    }
}

void ListDownloadTab::createList()
{
    ListDownloadTab::createList(this->type);
}

void ListDownloadTab::createList(contentType type)
{
    std::vector<std::pair<std::string, std::string>> links;
    if (type == contentType::cheats && this->newCheatsVer != "") {
        links.push_back(std::make_pair(fmt::format("menus/main/get_cheats"_i18n, this->newCheatsVer), CurrentCfw::running_cfw == CFW::sxos ? CHEATS_URL_TITLES : CHEATS_URL_CONTENTS));
        links.push_back(std::make_pair("menus/main/get_cheats_gfx"_i18n, CurrentCfw::running_cfw == CFW::sxos ? GFX_CHEATS_URL_TITLES : GFX_CHEATS_URL_CONTENTS));
    }
    else
        links = download::getLinksFromJson(util::getValueFromKey(this->nxlinks, contentTypeNames[(int)type].data()));

    if (links.size()) {
        for (const auto& link : links) {
            const std::string title = link.first;
            const std::string url = link.second;
            const std::string text("menus/common/download"_i18n + link.first);
            listItem = new brls::ListItem(link.first);
            listItem->setHeight(LISTITEM_HEIGHT);
            // Marcar las apps que ya están en la SD. Sólo se puede saber para
            // los .nro: los .zip no dicen dónde terminan sus archivos.
            if (type == contentType::apps && url.size() > 4 && url.substr(url.size() - 4) == ".nro") {
                if (std::filesystem::exists(util::resolveNroDestination(url.substr(url.rfind('/') + 1)))) {
                    listItem->setValue("menus/apps/installed"_i18n, true);
                }
            }
            listItem->getClickEvent()->subscribe([this, type, text, url, title](brls::View* view) {
                brls::StagedAppletFrame* stagedFrame = new brls::StagedAppletFrame();
                stagedFrame->setTitle(fmt::format("menus/main/getting"_i18n, contentTypeNames[(int)type].data()));
                stagedFrame->addStage(new ConfirmPage(stagedFrame, text));
                if (type == contentType::fw) {
                    std::string contentsPath = util::getContentsPath();
                    for (const auto& tid : {"0100000000001000", "0100000000001007", "0100000000001013"}) {
                        if (std::filesystem::exists(contentsPath + tid) && !std::filesystem::is_empty(contentsPath + tid)) {
                            stagedFrame->addStage(new DialoguePage_confirm(stagedFrame, "menus/main/theme_warning"_i18n));
                        }
                    }
                }
                if (type == contentType::apps) {
                    // La ruta del NRO se conoce de entrada si el link es un .nro;
                    // si viene en zip, la descubre el worker de extracción.
                    auto nroPath = std::make_shared<std::string>();

                    if (url.size() > 4 && url.substr(url.size() - 4) == ".nro") {
                        const std::string dest = util::resolveNroDestination(url.substr(url.rfind('/') + 1));
                        *nroPath = dest;
                        fs::createTree(dest.substr(0, dest.rfind('/') + 1));
                        stagedFrame->addStage(new WorkerPage(stagedFrame, "menus/common/downloading"_i18n, [url, dest]() { download::downloadFile(url, dest, OFF); }));
                    }
                    else {
                        stagedFrame->addStage(new WorkerPage(stagedFrame, "menus/common/downloading"_i18n, [url]() { util::downloadArchive(url, contentType::apps); }));
                        stagedFrame->addStage(new WorkerPage(stagedFrame, "menus/common/extracting"_i18n, [nroPath]() {
                            // Se anota antes de extraer, mientras el zip sigue en disco.
                            *nroPath = extract::findNroInArchive(CUSTOM_FILENAME);
                            util::extractArchive(contentType::apps);
                        }));
                    }

                    this->addForwarderStages(stagedFrame, nroPath);
                    brls::Application::pushView(stagedFrame);
                    return;
                }
                else if (type != contentType::payloads && type != contentType::hekate_ipl) {
                    if (type != contentType::cheats || (this->newCheatsVer != this->currentCheatsVer && this->newCheatsVer != "offline")) {
                        stagedFrame->addStage(new WorkerPage(stagedFrame, "menus/common/downloading"_i18n, [this, type, url]() { util::downloadArchive(url, type); }));
                    }
                    stagedFrame->addStage(new WorkerPage(stagedFrame, "menus/common/extracting"_i18n, [this, type]() { util::extractArchive(type, this->newCheatsVer); }));
                }
                else if (type == contentType::payloads) {
                    fs::createTree(BOOTLOADER_PL_PATH);
                    std::string path = std::string(BOOTLOADER_PL_PATH) + title;
                    stagedFrame->addStage(new WorkerPage(stagedFrame, "menus/common/downloading"_i18n, [url, path]() { download::downloadFile(url, path, OFF); }));
                }
                else if (type == contentType::hekate_ipl) {
                    fs::createTree(BOOTLOADER_PATH);
                    std::string path = std::string(BOOTLOADER_PATH) + title;
                    stagedFrame->addStage(new WorkerPage(stagedFrame, "menus/common/downloading"_i18n, [url, path]() { download::downloadFile(url, path, OFF); }));
                }

                std::string doneMsg = "menus/common/all_done"_i18n;
                if (type == contentType::fw && std::filesystem::exists(DAYBREAK_PATH)) {
                        stagedFrame->addStage(new DialoguePage_fw(stagedFrame, doneMsg));
                }
                else {
                    stagedFrame->addStage(new ConfirmPage_Done(stagedFrame, doneMsg));
                }
                brls::Application::pushView(stagedFrame);
            });
            this->addView(listItem);
        }
    }
    else {
        this->displayNotFound();
    }
}

void ListDownloadTab::addForwarderStages(brls::StagedAppletFrame* stagedFrame, std::shared_ptr<std::string> nroPath)
{
    auto makeForwarder = std::make_shared<bool>(false);
    auto forwarderResult = std::make_shared<std::string>();

    // Si nroPath sigue vacío al llegar acá, el zip no traía ningún .nro y la
    // pregunta se saltea sola.
    stagedFrame->addStage(new DialoguePage_optional(stagedFrame, "menus/apps/forwarder_ask"_i18n, makeForwarder, nroPath));
    stagedFrame->addStage(new WorkerPage(stagedFrame, "menus/apps/forwarder_creating"_i18n, [nroPath, makeForwarder, forwarderResult]() {
        auto& progress = ProgressEvent::instance();

        if (*makeForwarder && !nroPath->empty()) {
            fwd::Config config;
            Result rc = fwd::configFromNro(*nroPath, config);
            if (R_SUCCEEDED(rc)) {
                rc = fwd::install(config, [](int step, int total) {
                    ProgressEvent::instance().setTotalSteps(total);
                    ProgressEvent::instance().setStep(step);
                });
            }

            // El tid de un forwarder es el hash de la ruta del NRO, y esa ruta
            // cambia si la app pasa de /switch/x.nro a /switch/x/x.nro o al
            // revés. Reinstalar entonces NO sobrescribe el forwarder viejo:
            // quedan dos en el menú HOME y el viejo apunta a un archivo que ya
            // no está, así que da error al abrirlo. Borramos los de las demás
            // ubicaciones posibles de este mismo .nro.
            if (R_SUCCEEDED(rc)) {
                const std::string filename = nroPath->substr(nroPath->rfind('/') + 1);
                for (const auto& candidate : util::nroCandidatePaths(filename)) {
                    if (candidate != *nroPath) {
                        fwd::remove(candidate);
                    }
                }
            }
            // El diálogo no se puede abrir desde este hilo, así que el mensaje
            // se deja acá y lo muestra la página siguiente.
            *forwarderResult = R_SUCCEEDED(rc)
                                   ? "menus/common/all_done"_i18n
                                   : fmt::format("menus/apps/forwarder_error"_i18n, fwd::resultToString(rc));
        }
        else {
            *forwarderResult = "menus/common/all_done"_i18n;
        }

        progress.setStep(progress.getMax());
    }));
    stagedFrame->addStage(new ConfirmPage_Deferred(stagedFrame, forwarderResult));
}

void ListDownloadTab::displayNotFound()
{
    brls::Label* notFound = new brls::Label(
        brls::LabelStyle::SMALL,
        "menus/main/links_not_found"_i18n,
        true);
    notFound->setHorizontalAlign(NVG_ALIGN_CENTER);
    this->addView(notFound);
}

void ListDownloadTab::setDescription()
{
    this->setDescription(this->type);
}

void ListDownloadTab::setDescription(contentType type)
{
    brls::Label* description = new brls::Label(brls::LabelStyle::DESCRIPTION, "", true);

    switch (type) {
        case contentType::fw: {
            SetSysFirmwareVersion ver;
            description->setText(fmt::format("{}{}", "menus/main/firmware_text"_i18n, R_SUCCEEDED(setsysGetFirmwareVersion(&ver)) ? ver.display_version : "menus/main/not_found"_i18n));
            break;
        }
        case contentType::bootloaders:
            description->setText(
                "menus/main/bootloaders_text"_i18n);
            break;
        case contentType::cheats:
            this->newCheatsVer = util::getCheatsVersion();
            this->currentCheatsVer = util::readFile(CHEATS_VERSION);
            description->setText("menus/main/cheats_text"_i18n + this->currentCheatsVer);
            break;
        case contentType::payloads:
            description->setText(fmt::format("menus/main/payloads_label"_i18n, BOOTLOADER_PL_PATH));
            break;
        case contentType::hekate_ipl:
            description->setText("menus/main/hekate_ipl_label"_i18n);
            break;
        case contentType::apps:
            description->setText("menus/main/apps_text"_i18n);
            break;
        default:
            break;
    }

    this->addView(description);
}

void ListDownloadTab::createCheatSlipItem()
{
    brls::ListItem* cheatslipsItem = new brls::ListItem("menus/cheats/get_cheatslips"_i18n);
    cheatslipsItem->setHeight(LISTITEM_HEIGHT);
    cheatslipsItem->getClickEvent()->subscribe([](brls::View* view) {
        if (std::filesystem::exists(TOKEN_PATH)) {
            brls::Application::pushView(new AppPage_CheatSlips());
        }
        else {
            std::string usr, pwd;
            // Result rc = swkbdCreate(&kbd, 0);
            brls::Swkbd::openForText([&usr](std::string text) { usr = text; }, "cheatslips.com e-mail", "", 64, "", 0, "Submit", "cheatslips.com e-mail");
            brls::Swkbd::openForText([&pwd](std::string text) { pwd = text; }, "cheatslips.com password", "", 64, "", 0, "Submit", "cheatslips.com password", true);
            std::string body = "{\"email\":\"" + std::string(usr) + "\",\"password\":\"" + std::string(pwd) + "\"}";
            nlohmann::ordered_json token;
            download::getRequest(CHEATSLIPS_TOKEN_URL, token,
                                 {"Accept: application/json",
                                  "Content-Type: application/json",
                                  "charset: utf-8"},
                                 body);
            if (token.find("token") != token.end()) {
                std::ofstream tokenFile(TOKEN_PATH);
                tokenFile << token.dump();
                tokenFile.close();
                brls::Application::pushView(new AppPage_CheatSlips());
            }
            else {
                util::showDialogBoxInfo("menus/cheats/cheatslips_wrong_id"_i18n + "\n" + "menus/cheats/kb_error"_i18n);
            }
        }
        return true;
    });
    this->addView(cheatslipsItem);
}

void ListDownloadTab::createGbatempItem()
{
    brls::ListItem* gbatempItem = new brls::ListItem("menus/cheats/get_gbatemp"_i18n);
    gbatempItem->setHeight(LISTITEM_HEIGHT);
    gbatempItem->getClickEvent()->subscribe([](brls::View* view) {
        brls::Application::pushView(new AppPage_Gbatemp());
        return true;
    });
    this->addView(gbatempItem);
}

void ListDownloadTab::createGfxItem()
{
    brls::ListItem* gfxItem = new brls::ListItem("menus/cheats/get_gfx"_i18n);
    gfxItem->setHeight(LISTITEM_HEIGHT);
    gfxItem->getClickEvent()->subscribe([](brls::View* view) {
        brls::Application::pushView(new AppPage_Gfx());
        return true;
    });
    this->addView(gfxItem);
}