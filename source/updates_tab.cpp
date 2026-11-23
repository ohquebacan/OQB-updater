#include "updates_tab.hpp"

#include <string>

#include "constants.hpp"
#include "github_auth.hpp"
#include "utils.hpp"
#include "user_links_tab.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {

    void askForConfirmation(ghauth::DeviceCode code, int attempt);

    void loginFailed(const std::string& message)
    {
        brls::Application::notify(message);
    }

    // Shows the code; "Done" asks GitHub once whether it was confirmed.
    void showCodeDialog(const ghauth::DeviceCode& code)
    {
        brls::Dialog* dialog = new brls::Dialog(fmt::format("updates/login_code"_i18n, code.verificationUri, code.userCode));
        dialog->addButton("updates/login_done"_i18n, [dialog, code](brls::View* view) {
            dialog->close([code]() { askForConfirmation(code, 0); });
        });
        dialog->addButton("updates/login_cancel"_i18n, [dialog](brls::View* view) { dialog->close(); });
        dialog->setCancelable(true);
        dialog->open();
    }

    void askForConfirmation(ghauth::DeviceCode code, int attempt)
    {
        const ghauth::PollResult r = ghauth::pollOnce(code.deviceCode);
        switch (r.state) {
            case ghauth::Poll::granted:
                brls::Application::notify("updates/login_ok"_i18n);
                util::restartApp();
                break;
            case ghauth::Poll::pending:
            case ghauth::Poll::slowDown:
                brls::Application::notify("updates/login_pending"_i18n);
                showCodeDialog(code);
                break;
            case ghauth::Poll::denied:
            case ghauth::Poll::expired:
                loginFailed("updates/login_failed"_i18n);
                break;
            default:
                loginFailed("updates/login_err_net"_i18n);
                break;
        }
    }

    void startLogin()
    {
        const ghauth::DeviceCode code = ghauth::requestCode();
        if (!code.ok) {
            loginFailed("updates/login_err_net"_i18n);
            return;
        }
        showCodeDialog(code);
    }

    void askLogout()
    {
        brls::Dialog* dialog = new brls::Dialog("updates/logout_confirm"_i18n);
        dialog->addButton("menus/common/yes"_i18n, [dialog](brls::View* view) {
            ghauth::logout();
            dialog->close();
            util::restartApp();
        });
        dialog->addButton("menus/common/no"_i18n, [dialog](brls::View* view) { dialog->close(); });
        dialog->setCancelable(true);
        dialog->open();
    }

}  // namespace

UpdatesTab::UpdatesTab() : brls::List()
{
    this->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "updates/description"_i18n, true));

    // Signed in: GitHub allows 5000 requests/hour, so the check can run at every start.
    // Otherwise (60/hour) it only uses the saved answers and the user starts a check by hand.
    const bool loggedIn = ghauth::isLoggedIn();
    const std::vector<updates::Item> items = updates::scan(loggedIn);

    brls::ListItem* login = new brls::ListItem("updates/login"_i18n);
    login->setHeight(LISTITEM_HEIGHT);
    login->setValue(loggedIn ? "updates/logged_in"_i18n : "updates/logged_out"_i18n, false, false);
    login->getClickEvent()->subscribe([loggedIn](brls::View* view) {
        if (loggedIn) askLogout();
        else startLogin();
    });
    this->addView(login);

    if (!loggedIn) {
        brls::ListItem* check = new brls::ListItem("updates/check_now"_i18n);
        check->setHeight(LISTITEM_HEIGHT);
        check->getClickEvent()->subscribe([](brls::View* view) {
            updates::scan(true);
            if (updates::rateLimited()) util::showDialogBoxInfo("updates/rate_limit_hint"_i18n);
            else util::restartApp();
        });
        this->addView(check);
    }
    if (updates::rateLimited() && !loggedIn)
        this->addView(new brls::Label(brls::LabelStyle::SMALL, "updates/rate_limit_hint"_i18n, true));

    int available = 0;
    for (const auto& it : items)
        if (it.status == updates::Status::updateAvailable) available++;
    static bool notified = false;
    if (available > 0 && !notified) {
        notified = true;
        brls::Application::notify(fmt::format("updates/notify"_i18n, available));
    }

    if (items.empty()) {
        this->addView(new brls::Label(brls::LabelStyle::SMALL, "updates/empty"_i18n, true));
        return;
    }

    this->addSection(items, updates::Status::updateAvailable, "updates/section_update"_i18n);
    this->addSection(items, updates::Status::unknown, "updates/section_unknown"_i18n);
    this->addSection(items, updates::Status::upToDate, "updates/section_current"_i18n);
}

void UpdatesTab::addSection(const std::vector<updates::Item>& items, updates::Status status, const std::string& title)
{
    bool header = false;
    for (const auto& it : items) {
        if (it.status != status) continue;
        if (!header) {
            header = true;
            this->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, title, true));
        }

        brls::ListItem* item = new brls::ListItem(it.entry.name);
        item->setHeight(LISTITEM_HEIGHT);
        std::string value;
        if (status == updates::Status::updateAvailable) value = it.installed + " -> " + it.latest;
        else if (status == updates::Status::upToDate) value = it.installed;
        else value = it.latest.empty() ? "updates/no_info"_i18n : fmt::format("updates/latest"_i18n, it.latest);
        item->setValue(value, false, false);

        updates::Item copy = it;
        item->getClickEvent()->subscribe([copy](brls::View* view) { UserLinksTab::startInstall(updates::toLink(copy)); });
        this->addView(item);
    }
}
