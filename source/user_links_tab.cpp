#include "user_links_tab.hpp"

#include <cctype>

#include <memory>
#include <string>

#include "confirm_page.hpp"
#include "constants.hpp"
#include "download.hpp"
#include "extract.hpp"
#include "fs.hpp"
#include "progress_event.hpp"
#include "utils.hpp"
#include "worker_page.hpp"
#include "zip_layout.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;
using userlinks::Kind;
using userlinks::Link;

namespace {

    constexpr Kind SECTION_ORDER[] = {Kind::app, Kind::theme, Kind::cheats, Kind::mod, Kind::other, Kind::cfw};

    std::string kindName(Kind kind)
    {
        return i18n::getStr("user_links/kind_" + userlinks::kindKey(kind));
    }

    std::string sectionName(Kind kind)
    {
        return i18n::getStr("user_links/section_" + userlinks::kindKey(kind));
    }

    std::string errorText(const std::string& code)
    {
        if (code == "network" || code == "rate_limit" || code == "no_release" || code == "no_assets" || code == "no_match")
            return i18n::getStr("user_links/err_" + code);
        return i18n::getStr("user_links/err_unknown");
    }

    std::string trimmed(const std::string& s)
    {
        const size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return "";
        return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
    }

    // Runs in the worker thread: show the reason, then leave to the main screen.
    void failStage(const std::string& message)
    {
        util::showDialogBoxBlocking(message, "menus/common/ok"_i18n);
        ProgressEvent::instance().setInterupt(true);
        ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
    }

    struct InstallState
    {
        userlinks::Resolved resolved;
        bool extractNow = false;  // the downloaded file is a zip that has to be unpacked
    };

    bool endsWithZip(const std::string& name)
    {
        if (name.size() < 4) return false;
        std::string tail = name.substr(name.size() - 4);
        for (auto& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return tail == ".zip";
    }

    // Apps may ship a plain .nro or a zip: the zip case is unpacked too.
    bool wantsExtract(const Link& link, const std::string& fileName)
    {
        return link.extract || (link.kind == Kind::app && endsWithZip(fileName));
    }

    void downloadStage(const Link& link, std::shared_ptr<InstallState> state)
    {
        state->resolved = userlinks::resolve(link);
        const userlinks::Resolved& resolved = state->resolved;
        if (!resolved.ok) {
            failStage(errorText(resolved.error));
            return;
        }
        state->extractNow = wantsExtract(link, resolved.fileName);

        std::string target;
        if (state->extractNow) {
            fs::createTree(DOWNLOAD_PATH);
            target = CUSTOM_FILENAME;
        }
        else {
            fs::createTree(link.saveTo);
            target = link.saveTo + resolved.fileName;
        }

        long code = download::downloadFile(resolved.url, target, OFF);
        if (code < 200 || code >= 400) {
            ProgressEvent::instance().setStatusCode(code == 0 ? 503 : code);
            return;
        }
        if (!state->extractNow) userlinks::markInstalled(link, resolved.tag);
    }

    void extractStage(const Link& link, std::shared_ptr<InstallState> state)
    {
        if (!state->extractNow) return;  // it was a single file: already in place

        if (!util::isArchive(CUSTOM_FILENAME)) {
            failStage("user_links/err_not_zip"_i18n);
            return;
        }
        // directory used for zips that do not bring their own SD-card folders
        const std::string fallback = link.extract ? link.extractTo : link.saveTo;
        std::string dir = link.extractTo;
        std::string strip;
        if (link.autoLayout || !link.extract) {
            const ziplayout::Layout layout = ziplayout::detect(CUSTOM_FILENAME, fallback);
            dir = layout.dir;
            strip = layout.strip;
        }
        fs::createTree(dir);
        extract::extract(CUSTOM_FILENAME, dir, false, []() { return; }, strip);
        userlinks::markInstalled(link, state->resolved.tag);
    }

}  // namespace

UserLinksTab::UserLinksTab() : brls::List()
{
    const std::vector<Link> links = userlinks::load();

    // Once per app start: is there a newer release for any repo the user added?
    const int updates = userlinks::checkForUpdates(links);
    static bool notified = false;
    if (updates > 0 && !notified) {
        notified = true;
        brls::Application::notify(fmt::format("user_links/updates_notify"_i18n, updates));
    }

    this->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "user_links/description"_i18n, true));

    brls::ListItem* add = new brls::ListItem("user_links/add"_i18n);
    add->setHeight(LISTITEM_HEIGHT);
    add->getClickEvent()->subscribe([](brls::View* view) { UserLinksTab::openEditor(-1); });
    this->addView(add);

    if (links.empty()) {
        this->addView(new brls::Label(brls::LabelStyle::SMALL, "user_links/empty"_i18n, true));
        return;
    }

    for (Kind kind : SECTION_ORDER) this->addSection(links, kind);
}

void UserLinksTab::addSection(const std::vector<Link>& links, Kind kind)
{
    bool headerAdded = false;
    for (const auto& link : links) {
        if (link.kind != kind) continue;

        if (!headerAdded) {
            headerAdded = true;
            this->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, sectionName(kind), true));
        }

        brls::ListItem* item = new brls::ListItem(link.name);
        item->setHeight(LISTITEM_HEIGHT);
        if (kind == Kind::cfw) {
            item->setValue("user_links/cfw_value"_i18n, true);
        }
        else if (userlinks::updateAvailable(link)) {
            item->setValue("user_links/update_available"_i18n, false);
        }
        else if (!link.installedTag.empty()) {
            item->setValue(link.installedTag, true);
        }

        item->getClickEvent()->subscribe([link, kind](brls::View* view) {
            if (kind == Kind::cfw)
                util::showDialogBoxInfo("user_links/cfw_info"_i18n);
            else
                UserLinksTab::startInstall(link);
        });
        item->registerAction("user_links/remove"_i18n, brls::Key::X, [link] {
            UserLinksTab::askRemove(link);
            return true;
        });
        item->registerAction("user_links/edit"_i18n, brls::Key::Y, [link] {
            const auto all = userlinks::load();
            for (size_t i = 0; i < all.size(); i++) {
                if (all[i].url == link.url && all[i].name == link.name) {
                    UserLinksTab::openEditor(static_cast<int>(i));
                    break;
                }
            }
            return true;
        });
        this->addView(item);
    }
}

void UserLinksTab::askRemove(const Link& link)
{
    brls::Dialog* dialog = new brls::Dialog(fmt::format("user_links/remove_confirm"_i18n, link.name));
    dialog->addButton("menus/common/yes"_i18n, [dialog, link](brls::View* view) {
        std::vector<Link> all = userlinks::load();
        for (size_t i = 0; i < all.size(); i++) {
            if (all[i].url == link.url && all[i].name == link.name) {
                all.erase(all.begin() + static_cast<long>(i));
                break;
            }
        }
        userlinks::save(all);
        dialog->close();
        util::restartApp();
    });
    dialog->addButton("menus/common/no"_i18n, [dialog](brls::View* view) { dialog->close(); });
    dialog->setCancelable(true);
    dialog->open();
}

void UserLinksTab::startInstall(const Link& link)
{
    brls::StagedAppletFrame* staged = new brls::StagedAppletFrame();
    staged->setTitle(fmt::format("user_links/installing"_i18n, link.name));

    const std::string destination = link.extract ? fmt::format("user_links/dest_extract"_i18n, link.extractTo)
                                                 : fmt::format("user_links/dest_save"_i18n, link.saveTo);
    staged->addStage(new ConfirmPage(staged, fmt::format("user_links/confirm_download"_i18n, link.name, destination)));

    auto state = std::make_shared<InstallState>();
    staged->addStage(new WorkerPage(staged, "menus/common/downloading"_i18n, [link, state]() { downloadStage(link, state); }));
    if (link.extract || link.kind == Kind::app)
        staged->addStage(new WorkerPage(staged, "menus/common/extracting"_i18n, [link, state]() { extractStage(link, state); }));
    staged->addStage(new ConfirmPage_Done(staged, "menus/common/all_done"_i18n));
    brls::Application::pushView(staged);
}

void UserLinksTab::openEditor(int index)
{
    std::vector<Link> all = userlinks::load();
    const bool editing = index >= 0 && index < static_cast<int>(all.size());

    Link link;
    if (editing) {
        link = all[static_cast<size_t>(index)];
    }
    else {
        const auto d = userlinks::defaultsFor(link.kind);
        link.saveTo = d.saveTo;
        link.extract = d.extract;
        link.extractTo = d.extractTo;
    }

    std::vector<std::string> kindNames;
    for (int k = 0; k < userlinks::KIND_COUNT; k++) kindNames.push_back(kindName(static_cast<Kind>(k)));
    const std::vector<std::string> yesNo = {"user_links/no"_i18n, "user_links/yes"_i18n};

    brls::List* form = new brls::List();
    form->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "user_links/editor_help"_i18n, true));

    auto* urlItem = new brls::InputListItem("user_links/field_url"_i18n, link.url, "user_links/field_url"_i18n, "", 256);
    auto* nameItem = new brls::InputListItem("user_links/field_name"_i18n, link.name, "user_links/field_name"_i18n, "", 64);
    auto* kindItem = new brls::SelectListItem("user_links/field_kind"_i18n, kindNames, static_cast<unsigned>(link.kind));
    auto* saveItem = new brls::InputListItem("user_links/field_save_to"_i18n, link.saveTo, "user_links/field_save_to"_i18n, "", 128);
    auto* extractItem = new brls::SelectListItem("user_links/field_extract"_i18n, yesNo, link.extract ? 1u : 0u);
    auto* extractToItem = new brls::InputListItem("user_links/field_extract_to"_i18n, link.extractTo, "user_links/field_extract_to"_i18n, "", 128);
    auto* filterItem = new brls::InputListItem("user_links/field_filter"_i18n, link.filter, "user_links/field_filter"_i18n, "user_links/field_filter_desc"_i18n, 64);

    // Picking another kind fills in the usual folders for it (still editable afterwards).
    kindItem->getValueSelectedEvent()->subscribe([saveItem, extractItem, extractToItem](int selected) {
        if (selected < 0 || selected >= userlinks::KIND_COUNT) return;
        const auto d = userlinks::defaultsFor(static_cast<Kind>(selected));
        saveItem->setValue(d.saveTo, false, false);
        extractItem->setSelectedValue(d.extract ? 1u : 0u);
        extractToItem->setValue(d.extractTo, false, false);
    });

    brls::ListItem* saveButton = new brls::ListItem("user_links/save"_i18n);
    saveButton->setHeight(LISTITEM_HEIGHT);
    saveButton->getClickEvent()->subscribe([=](brls::View* view) {
        std::string url = trimmed(urlItem->getValue());
        userlinks::GithubRef ref;
        const bool plainUrl = url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
        if (!plainUrl && !userlinks::parseGithubUrl(url, ref)) {
            util::showDialogBoxInfo("user_links/err_invalid_url"_i18n);
            return;
        }
        if (!plainUrl) url = "https://" + url;

        Link result;
        result.url = url;
        result.name = trimmed(nameItem->getValue());
        if (result.name.empty()) result.name = userlinks::defaultNameFor(url);
        result.kind = static_cast<Kind>(kindItem->getSelectedValue());
        result.extract = extractItem->getSelectedValue() == 1;
        result.filter = trimmed(filterItem->getValue());

        result.saveTo = userlinks::normalizeDir(saveItem->getValue());
        result.extractTo = userlinks::normalizeDir(extractToItem->getValue());
        if (result.kind != Kind::cfw) {
            if (result.saveTo.empty()) {
                util::showDialogBoxInfo(fmt::format("user_links/err_invalid_path"_i18n, saveItem->getValue()));
                return;
            }
            if (result.extract && result.extractTo.empty()) {
                util::showDialogBoxInfo(fmt::format("user_links/err_invalid_path"_i18n, extractToItem->getValue()));
                return;
            }
        }
        if (result.saveTo.empty()) result.saveTo = "/";
        if (result.extractTo.empty()) result.extractTo = "/";

        std::vector<Link> links = userlinks::load();
        for (size_t i = 0; i < links.size(); i++) {
            if (static_cast<int>(i) != index && links[i].url == result.url && links[i].name == result.name) {
                util::showDialogBoxInfo("user_links/err_duplicate"_i18n);
                return;
            }
        }
        if (editing && index < static_cast<int>(links.size())) {
            if (links[static_cast<size_t>(index)].url == result.url) result.installedTag = links[static_cast<size_t>(index)].installedTag;
            links[static_cast<size_t>(index)] = result;
        }
        else {
            links.push_back(result);
        }
        userlinks::save(links);
        brls::Application::notify("user_links/saved"_i18n);
        util::restartApp();
    });

    form->addView(urlItem);
    form->addView(nameItem);
    form->addView(kindItem);
    form->addView(saveItem);
    form->addView(extractItem);
    form->addView(extractToItem);
    form->addView(filterItem);
    form->addView(saveButton);

    brls::AppletFrame* frame = new brls::AppletFrame(true, true);
    frame->setContentView(form);
    brls::PopupFrame::open(editing ? "user_links/editor_edit"_i18n : "user_links/editor_add"_i18n, frame, "", "");
}
