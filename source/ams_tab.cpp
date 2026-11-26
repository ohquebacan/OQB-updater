#include "github_auth.hpp"
#include "user_links.hpp"
#include "ams_tab.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>

#include "confirm_page.hpp"
#include "current_cfw.hpp"
#include "dialogue_page.hpp"
#include "download.hpp"
#include "extract.hpp"
#include "fs.hpp"
#include "utils.hpp"
#include "worker_page.hpp"
namespace i18n = brls::i18n;
using namespace i18n::literals;

AmsTab::AmsTab(const nlohmann::ordered_json& nxlinks, const bool erista) : brls::List()
{
    this->erista = erista;
    this->nxlinks = nxlinks;
    this->hekate = util::getValueFromKey(nxlinks, "hekate");
}

void AmsTab::RegisterListItemAction(brls::ListItem* listItem) {}

bool AmsTab::CreateDownloadItems(const nlohmann::ordered_json& cfw_links, bool hekate, bool ams)
{
    std::vector<std::pair<std::string, std::string>> links;
    links = download::getLinksFromJson(cfw_links);
    if (links.empty()) return false;

    // Stock hekate download (only offered for entries WITHOUT "[PACK]" in their name).
    std::string hekate_url, text_hekate;
    if (hekate && !this->hekate.empty()) {
        auto hekate_link = download::getLinksFromJson(this->hekate);
        if (!hekate_link.empty()) {
            hekate_url = hekate_link[0].second;
            text_hekate = "menus/common/download"_i18n + hekate_link[0].first;
        }
    }
    const bool canHekate = !hekate_url.empty();

    // The tab's `type` changes while the lists are built (custom tab: full pack
    // first, then misc), so remember it per item instead of reading it later.
    const contentType itemType = this->type;
    for (const auto& link : links) {
        bool pack = link.first.contains("[PACK]");
        std::string url = link.second;

        // "[PACK]" is only a marker for the logic, don't show it.
        std::string display = link.first;
        for (size_t pos; (pos = display.find("[PACK]")) != std::string::npos;) display.erase(pos, 6);
        while (!display.empty() && display.back() == ' ') display.pop_back();
        while (!display.empty() && display.front() == ' ') display.erase(display.begin());

        std::string text("menus/common/download"_i18n + display);
        listItem = new brls::ListItem(display);
        listItem->setHeight(LISTITEM_HEIGHT);
        listItem->getClickEvent()->subscribe([this, text, text_hekate, url, hekate_url, hekate, canHekate, pack, ams, itemType](brls::View* view) {
            if (!erista && !std::filesystem::exists(MARIKO_PAYLOAD_PATH)) {
                brls::Application::crash("menus/errors/mariko_payload_missing"_i18n);
            }
            else {
                CreateStagedFrames(text, url, erista, ams, hekate && canHekate && !pack, text_hekate, hekate_url, itemType);
            }
        });
        this->RegisterListItemAction(listItem);
        this->addView(listItem);
    }
    return true;
}

// Every .zip asset of YOUR GitHub releases whose release name starts with
// RELEASE_NAME_FILTER (newest first, as the GitHub API returns them).
// Fetched once per start: the unauthenticated API allows only 60 requests/hour.
nlohmann::ordered_json AmsTab::FetchGithubReleasePacks()
{
    static nlohmann::ordered_json cache = nlohmann::ordered_json::object();
    static bool fetched = false;
    if (fetched && !cache.empty()) return cache;
    fetched = true;

    nlohmann::ordered_json releases;
    download::getRequest(RELEASES_API_URL, releases, ghauth::apiHeaders());
    if (!releases.is_array()) releases = nlohmann::ordered_json::array();

    const auto str = [](const nlohmann::ordered_json& j, const char* key) -> std::string {
        auto it = j.find(key);
        return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string();
    };

    nlohmann::ordered_json packs = nlohmann::ordered_json::object();
    for (const auto& release : releases) {
        if (!release.is_object()) continue;
        auto draft = release.find("draft");
        if (draft != release.end() && draft->is_boolean() && draft->get<bool>()) continue;

        std::string name = str(release, "name");
        if (name.empty()) name = str(release, "tag_name");
        if (RELEASE_NAME_FILTER[0] != '\0' && name.rfind(RELEASE_NAME_FILTER, 0) != 0) continue;
        if (userlinks::isPcRelease(name)) continue;  // "(PC-Version)" releases are of no use on the Switch

        auto assets = release.find("assets");
        if (assets == release.end() || !assets->is_array()) continue;

        std::vector<userlinks::Asset> allZips;
        for (const auto& asset : *assets) {
            const std::string assetName = str(asset, "name");
            const std::string url = str(asset, "browser_download_url");
            if (url.empty() || assetName.size() < 5) continue;
            std::string ext = assetName.substr(assetName.size() - 4);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
            if (ext == ".zip") allZips.push_back({assetName, url});
        }
        // the PC version of a release is of no use on the Switch
        std::vector<std::pair<std::string, std::string>> zips;  // asset name, url
        for (const auto& z : userlinks::switchAssetsOnly(allZips)) zips.emplace_back(z.name, z.url);
        for (const auto& [assetName, url] : zips) {
            // "[PACK]" = the zip already brings its own hekate, don't offer the stock one afterwards.
            std::string label = zips.size() > 1 ? name + " - " + assetName : name;
            packs[label + " [PACK]"] = url;
        }
    }
    // CFW packs the user added in the "Eigene Links" tab show up in the same list.
    const nlohmann::ordered_json userPacks = userlinks::cfwPacks();
    for (auto it = userPacks.begin(); it != userPacks.end(); ++it) {
        if (!packs.contains(it.key())) packs[it.key()] = it.value();
    }
    cache = packs;
    return cache;
}

void AmsTab::CreateStagedFrames(const std::string& text, const std::string& url, bool erista, bool ams, bool hekate, const std::string& text_hekate, const std::string& hekate_url, contentType type)
{
    brls::StagedAppletFrame* stagedFrame = new brls::StagedAppletFrame();
    stagedFrame->setTitle(type == contentType::ams_cfw || type == contentType::full_pack ? "menus/ams_update/getting_ams"_i18n : "menus/ams_update/custom_download"_i18n);
    stagedFrame->addStage(
        new ConfirmPage(stagedFrame, text));
    stagedFrame->addStage(
        new WorkerPage(stagedFrame, "menus/common/downloading"_i18n, [url, type]() { util::downloadArchive(url, type); }));
    stagedFrame->addStage(
        new WorkerPage(stagedFrame, "menus/common/extracting"_i18n, [type]() { util::extractArchive(type); }));
    if (hekate) {
        stagedFrame->addStage(
            new DialoguePage_ams(stagedFrame, text_hekate, erista));
        stagedFrame->addStage(
            new WorkerPage(stagedFrame, "menus/common/downloading"_i18n, [hekate_url]() { util::downloadArchive(hekate_url, contentType::bootloaders); }));
        stagedFrame->addStage(
            new WorkerPage(stagedFrame, "menus/common/extracting"_i18n, []() { util::extractArchive(contentType::bootloaders); }));
    }
    if (ams)
        stagedFrame->addStage(new ConfirmPage_AmsUpdate(stagedFrame, "menus/ams_update/reboot_rcm"_i18n, erista));
    else
        stagedFrame->addStage(new ConfirmPage_Done(stagedFrame, "menus/common/all_done"_i18n));
    brls::Application::pushView(stagedFrame);
}

AmsTab_Regular::AmsTab_Regular(const nlohmann::ordered_json& nxlinks, const bool erista) : AmsTab(nxlinks, erista)
{
    this->CreateLists();
}

bool AmsTab_Regular::CreateDownloadItems(const nlohmann::ordered_json& cfw_links, bool hekate, bool ams)
{
    if (!AmsTab::CreateDownloadItems(cfw_links, hekate, ams)) {
        brls::Label* description = new brls::Label(
            brls::LabelStyle::SMALL,
            "menus/main/links_not_found"_i18n,
            true);
        description->setHorizontalAlign(NVG_ALIGN_CENTER);
        this->addView(description);
        return true;
    }
    return false;
}

void AmsTab_Regular::CreateLists()
{
    // Full-pack flow (download -> verify -> optional SD wipe -> extract) for every
    // pack of your GitHub releases. The stock lists (OQB HATS PACK, Overclock
    // HATSPACK, DeepSea ...) are intentionally not shown any more.
    this->type = contentType::full_pack;

    this->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/main/ams_text"_i18n + (CurrentCfw::running_cfw == CFW::ams ? "\n" + "menus/ams_update/current_ams"_i18n + CurrentCfw::getAmsInfo() : "") + (erista ? "\n" + "menus/ams_update/erista_rev"_i18n : "\n" + "menus/ams_update/mariko_rev"_i18n), true));
    CreateDownloadItems(this->FetchGithubReleasePacks(), false);
}

std::string AmsTab_Regular::GetRepoName(const std::string& repo)
{
    return repo.substr(repo.find("/") + 1, repo.length());
}

std::set<std::string> AmsTab_Regular::GetLastDownloadedModules(const std::string& json_path)
{
    nlohmann::ordered_json package = fs::parseJsonFile(json_path);
    std::set<std::string> res;
    if (package.find("modules") != package.end()) {
        for (const auto& module : package.at("modules")) {
            res.insert(module.get<std::string>());
        }
    }
    return res;
}

nlohmann::ordered_json AmsTab_Regular::SortDeepseaModules(const nlohmann::ordered_json& modules)
{
    nlohmann::ordered_json sorted_modules = nlohmann::ordered_json::object();
    if (modules.find("modules") != modules.end()) {
        for (const auto& module : modules.at("modules").items()) {
            sorted_modules[std::string(module.value().at("category"))][module.key()] = module.value();
        }
    }
    return sorted_modules;
}

void AmsTab_Regular::ShowCustomDeepseaBuilder(nlohmann::ordered_json& modules)
{
    modules = SortDeepseaModules(modules);
    std::map<std::string, std::string> name_map;

    brls::TabFrame* appView = new brls::TabFrame();
    appView->setIcon("romfs:/deepsea_icon.png");

    std::vector<brls::List*> lists;
    std::set<std::string> old_modules = GetLastDownloadedModules(DEEPSEA_PACKAGE_PATH);

    brls::ToggleListItem* deepseaListItem;
    for (const auto& category : modules.items()) {
        brls::List* list = new brls::List();

        for (const auto& module : category.value().items()) {
            auto module_value = module.value();
            std::string requirements = "";
            if (!module_value.at("requires").empty()) {
                requirements = "menus/ams_update/depends_on"_i18n;
                for (const auto& r : module.value().at("requires")) {
                    requirements += " " + r.get<std::string>() + ",";
                }
                requirements.pop_back();
            }
            if (module_value.at("required")) {
                deepseaListItem = new UnTogglableListItem(module_value.at("displayName"), 1, requirements, "Required", "o");
            }
            else {
                deepseaListItem = new ::brls::ToggleListItem(module_value.at("displayName"),
                                                             old_modules.find(module.key()) != old_modules.end() ? 1 : 0,
                                                             requirements,
                                                             "menus/common/selected"_i18n,
                                                             "menus/common/off"_i18n);
            }
            name_map.insert(std::pair(module_value.at("displayName"), module.key()));
            deepseaListItem->registerAction("menus/ams_update/show_module_description"_i18n, brls::Key::Y, [module_value] {
                util::showDialogBoxInfo(fmt::format("{}:\n{}", module_value.at("repo"), module_value.at("description")));
                return true;
            });
            list->addView(deepseaListItem);
        }
        lists.push_back(list);
        appView->addTab(category.key(), list);
    }

    appView->registerAction("menus/ams_update/download_deepsea_package"_i18n, brls::Key::X, [this, lists, name_map] {
        std::set<std::string> desired_modules;
        for (const auto& list : lists) {
            for (size_t i = 0; i < list->getViewsCount(); i++) {
                if (brls::ToggleListItem* item = dynamic_cast<brls::ToggleListItem*>(list->getChild(i))) {
                    if (item->getToggleState()) {
                        desired_modules.insert(name_map.at(item->getLabel()));
                    }
                }
            }
        }

        std::string request_url = DEEPSEA_BUILD_URL;
        for (const auto& e : desired_modules)
            request_url += e + ";";

        this->CreateStagedFrames("menus/common/download"_i18n + "Custom DeepSea package" + "menus/common/from"_i18n + request_url,
                                 request_url,
                                 this->erista);
        return true;
    });
    appView->registerAction("", brls::Key::PLUS, [this] { return true; });

    brls::PopupFrame::open("menus/ams_update/deepsea_builder"_i18n, appView, modules.empty() ? "menus/ams_update/cant_fetch_deepsea"_i18n : "menus/ams_update/build_your_deepsea"_i18n, "");
}

AmsTab_Custom::AmsTab_Custom(const nlohmann::ordered_json& nxlinks, const bool erista) : AmsTab(nxlinks, erista)
{
    // Link list comes from YOUR GitHub (CUSTOM_PACKS_URL), unless the SD card has
    // /config/aio-switch-updater/links_url.txt with another URL on its first line.
    std::string linksUrl = CUSTOM_PACKS_URL;
    for (const auto& line : fs::readLineByLine(LINKS_URL_OVERRIDE)) {
        std::string trimmed = line;
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
        if (trimmed.rfind("http", 0) == 0) {
            linksUrl = trimmed;
            break;
        }
    }
    download::getRequest(linksUrl, this->custom_packs);
    if (this->custom_packs.empty())
        this->custom_packs = fs::parseJsonFile(CUSTOM_PACKS_PATH);
    if (!this->custom_packs.is_object())
        this->custom_packs = nlohmann::ordered_json::object();
    this->AddGithubReleases();
    this->CreateLists();
}

// GitHub releases first, then anything from custom_packs.json.
void AmsTab_Custom::AddGithubReleases()
{
    nlohmann::ordered_json ams = this->FetchGithubReleasePacks();
    auto manual = this->custom_packs.find("ams");
    if (manual != this->custom_packs.end() && manual->is_object()) {
        for (const auto& item : manual->items()) {
            if (!ams.contains(item.key())) ams[item.key()] = item.value();
        }
    }
    this->custom_packs["ams"] = ams;
}

void AmsTab_Custom::CreateLists()
{
    this->addView(new brls::Label(
        brls::LabelStyle::DESCRIPTION,
        fmt::format("menus/ams_update/custom_packs_label"_i18n, CUSTOM_PACKS_PATH),
        true));

    this->type = contentType::full_pack;  // download -> verify -> optional SD wipe -> extract
    this->addView(new brls::Label(
        brls::LabelStyle::DESCRIPTION,
        "menus/ams_update/custom_packs_ams"_i18n,
        true));
    CreateDownloadItems(util::getValueFromKey(this->custom_packs, "ams"), true);
    this->AddLinkCreator();

    this->type = contentType::custom;
    this->addView(new brls::Label(
        brls::LabelStyle::DESCRIPTION,
        "menus/ams_update/custom_packs_misc"_i18n,
        true));
    CreateDownloadItems(util::getValueFromKey(this->custom_packs, "misc"), false, false);
    this->AddLinkCreator();
}

void AmsTab_Custom::AddLinkCreator()
{
    std::string category = (this->type == contentType::ams_cfw || this->type == contentType::full_pack) ? "ams" : "misc";
    listItem = new brls::ListItem("menus/ams_update/add_custom_link"_i18n);
    listItem->setHeight(LISTITEM_HEIGHT);
    listItem->getClickEvent()->subscribe([this, category](brls::View* view) {
        std::string title, link;
        brls::Swkbd::openForText([&title](std::string text) { title = text; }, "Enter title", "", 256, "", 0, "Submit", "Title");
        brls::Swkbd::openForText([&link](std::string text) { link = text; }, "Enter direct link", "", 256, "", 0, "Submit", "https://site/download.zip");
        auto links = util::getValueFromKey(this->custom_packs, category);
        links[title] = link;
        this->custom_packs[category] = links;
        fs::writeJsonToFile(this->custom_packs, CUSTOM_PACKS_PATH);
        util::restartApp();
    });
    this->addView(listItem);
}

void AmsTab_Custom::RegisterListItemAction(brls::ListItem* listItem)
{
    std::string label = listItem->getLabel();
    std::string category = (this->type == contentType::ams_cfw || this->type == contentType::full_pack) ? "ams" : "misc";
    listItem->registerAction("menus/ams_update/delete_custom_link"_i18n, brls::Key::X, [this, label, category] {
        auto& links = this->custom_packs.at(category);
        links.erase(label);
        fs::writeJsonToFile(this->custom_packs, CUSTOM_PACKS_PATH);
        util::restartApp();
        return true;
    });
}

bool UnTogglableListItem::onClick()
{
    return true;
}
