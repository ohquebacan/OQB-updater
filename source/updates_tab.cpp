#include "updates_tab.hpp"

#include <string>

#include "constants.hpp"
#include "user_links_tab.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

UpdatesTab::UpdatesTab() : brls::List()
{
    this->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "updates/description"_i18n, true));

    const std::vector<updates::Item> items = updates::scan(true);

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
