#include "about_tab.hpp"

#include <switch.h>

#include <algorithm>
#include <vector>

#include "app_update.hpp"
#include "constants.hpp"
#include "current_cfw.hpp"
#include "fs.hpp"
#include "protection.hpp"
#include "protection_page.hpp"
#include "utils.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {

    constexpr const char AppVersion[] = APP_VERSION;

    // Un valor que no se pudo leer se dice, no se inventa ni se deja en blanco.
    std::string unknown()
    {
        return "menus/about/status_unknown"_i18n;
    }

    brls::ListItem* row(const std::string& label, const std::string& value, bool faint = false)
    {
        brls::ListItem* item = new brls::ListItem(label);
        item->setHeight(LISTITEM_HEIGHT);
        item->setValue(value, faint);
        return item;
    }

    std::string systemFirmware()
    {
        SetSysFirmwareVersion ver;
        if (R_FAILED(setsysGetFirmwareVersion(&ver)))
            return unknown();
        return ver.display_version;
    }

    // getAmsInfo() devuelve "1.11.2|E" o "1.11.2|S" — version y de donde
    // arranco — o un texto de error si no pudo leerla.
    std::string customFirmware()
    {
        if (CurrentCfw::running_cfw == CFW::rnx)
            return "ReiNX";
        if (CurrentCfw::running_cfw == CFW::sxos)
            return "SX OS";

        const std::string info = CurrentCfw::getAmsInfo();
        const size_t sep = info.find('|');
        if (sep == std::string::npos)
            return unknown();

        const std::string version = info.substr(0, sep);
        const std::string source = info.substr(sep + 1);
        return fmt::format("Atmosphère {} ({})", version,
                           source == "E" ? "emuNAND" : "sysNAND");
    }

    std::string freeSpace()
    {
        s64 free;
        if (R_FAILED(fs::getFreeStorageSD(free)))
            return unknown();
        return fmt::format("{:.1f} GB", (float)free / 0x40000000);
    }

}  // namespace

AboutTab::AboutTab(const std::string& tag) : brls::List()
{
    this->addStatusPanel(tag);
    this->addAppInfo();
}

void AboutTab::addStatusPanel(const std::string& tag)
{
    this->addView(new brls::Header("menus/about/status_title"_i18n));

    // La version propia primero, y si hay una nueva se dice aqui mismo en vez
    // de dejarlo solo en el pie de pantalla, que pasa desapercibido.
    const bool hasUpdate = appUpdate::isAvailable(tag);
    brls::ListItem* version = row("menus/about/status_version"_i18n,
                                  hasUpdate ? fmt::format("{} → {}", AppVersion, tag) : AppVersion);
    if (hasUpdate) {
        version->setSubLabel("menus/about/status_version_new"_i18n);
        version->getClickEvent()->subscribe([tag](brls::View* view) {
            appUpdate::pushFlow(tag, true);
        });
    }
    this->addView(version);

    this->addView(row("menus/about/status_cfw"_i18n, customFirmware()));
    this->addView(row("menus/about/status_firmware"_i18n, systemFirmware()));
    this->addView(row("menus/about/status_model"_i18n,
                      util::isErista() ? "Erista (v1)" : "Mariko (v2)"));
    this->addView(row("menus/about/status_sd"_i18n, freeSpace()));

    /* Las protecciones, resumidas. Es el dato por el que existe el verificador:
       rotar el package3 puede dejar la consola sin bloqueo DNS sin avisar, y
       nadie entra a mirarlo si no se le dice que hay algo que mirar. La fila
       abre el verificador completo. */
    const std::vector<protection::Check> checks = protection::run();
    const bool failed = protection::anyFailed(checks);
    const size_t failures = std::count_if(checks.begin(), checks.end(),
                                          [](const protection::Check& c) {
                                              return c.status == protection::Status::Fail;
                                          });

    brls::ListItem* prot = row("menus/about/status_protection"_i18n,
                               failed ? "menus/about/status_protection_fail"_i18n
                                      : "menus/about/status_protection_ok"_i18n);
    // Una comprobacion y varias no se dicen igual, asi que son dos textos en vez
    // de uno con un numero encajado a la fuerza.
    prot->setSubLabel(!failed ? "menus/about/status_protection_ok_sub"_i18n
                      : failures == 1
                          ? "menus/about/status_protection_fail_one"_i18n
                          : fmt::format("menus/about/status_protection_fail_many"_i18n, failures));
    prot->getClickEvent()->subscribe([](brls::View* view) {
        brls::PopupFrame::open("menus/tools/protection"_i18n, new ProtectionPage(),
                               "menus/tools/protection_desc"_i18n, "");
    });
    this->addView(prot);
}

void AboutTab::addAppInfo()
{
    this->addView(new brls::Header("menus/about/disclaimers_title"_i18n));

    brls::Label* subTitle = new brls::Label(
        brls::LabelStyle::REGULAR,
        "menus/about/title"_i18n + "\nOH! QUÉ BACÁN",
        true);
    subTitle->setHorizontalAlign(NVG_ALIGN_CENTER);
    this->addView(subTitle);

    brls::Label* links = new brls::Label(
        brls::LabelStyle::SMALL,
        "menus/about/copyright"_i18n + "\n" + "menus/about/disclaimers"_i18n,
        true);
    this->addView(links);
}
