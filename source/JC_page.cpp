#include "JC_page.hpp"

#include "color_picker_page.hpp"
#include "color_swapper.hpp"
#include "confirm_page.hpp"
#include "constants.hpp"
#include "worker_page.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;
JCPage::JCPage() : AppletFrame(true, true)
{
    this->setTitle("menus/joy_con/title"_i18n);
    list = new brls::List();
    label = new brls::Label(brls::LabelStyle::DESCRIPTION, fmt::format("menus/joy_con/description"_i18n, JC_COLOR_PATH, COLOR_PICKER_URL), true);
    list->addView(label);

    backup = new brls::ListItem("menus/joy_con/backup"_i18n);
    backup->getClickEvent()->subscribe([](brls::View* view) {
        brls::StagedAppletFrame* stagedFrame = new brls::StagedAppletFrame();
        stagedFrame->setTitle("menus/joy_con/label"_i18n);
        stagedFrame->addStage(
            new WorkerPage(stagedFrame, "menus/joy_con/backing_up"_i18n,
                           []() { JC::backupJCColor(JC_COLOR_PATH); }));
        stagedFrame->addStage(
            new ConfirmPage_Done(stagedFrame, "menus/common/all_done"_i18n));
        brls::Application::pushView(stagedFrame);
    });
    list->addView(backup);

    // Selector de color personalizado (sliders RGB en vivo)
    customColor = new brls::ListItem("menus/color_picker/entry"_i18n);
    customColor->getClickEvent()->subscribe([](brls::View* view) {
        brls::AppletFrame* appView = new brls::AppletFrame(true, true);
        appView->setContentView(new ColorPickerPage(ColorPickerPage::Controller::JoyCon));
        brls::PopupFrame::open(
            "menus/color_picker/title"_i18n,
            appView,
            "menus/color_picker/hint"_i18n,
            "");
    });
    list->addView(customColor);

    list->addView(new brls::ListItemGroupSpacing(true));

    auto profiles = JC::getProfiles(JC_COLOR_PATH);
    for (const auto& profile : profiles) {
        std::vector<int> value = profile.second;
        listItem = new brls::ListItem(profile.first);
        listItem->getClickEvent()->subscribe([value](brls::View* view) {
            brls::StagedAppletFrame* stagedFrame = new brls::StagedAppletFrame();
            stagedFrame->setTitle("menus/joy_con/label"_i18n);
            stagedFrame->addStage(
                new WorkerPage(stagedFrame, "menus/joy_con/changing"_i18n,
                               [value]() { JC::changeJCColor(value); }));
            stagedFrame->addStage(
                new ConfirmPage_Done(stagedFrame, "menus/joy_con/all_done"_i18n));
            brls::Application::pushView(stagedFrame);
        });
        list->addView(listItem);
    }
    this->setContentView(list);
}