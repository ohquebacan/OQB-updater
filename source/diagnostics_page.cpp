#include "diagnostics_page.hpp"

#include "nexus_tab.hpp"
#include "soporte.hpp"

#include <switch.h>

#include <memory>

#include "constants.hpp"
#include "crash_report.hpp"
#include "utils.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

/* Los juegos y los forwarders estan instalados como titulos, asi que el
   sistema sabe su nombre; para los programas del sistema que mas fallan se
   pone a mano. Si no hay nada, el modulo donde fallo ("tinfoil") dice mas que
   el id. */
std::string crashProgramName(const crash::Group& g)
{
    if (g.programId == "0100000000001000") return "menus/diagnostics/name_home"_i18n;
    if (g.programId == "010000000000100d") return "menus/diagnostics/name_hbl"_i18n;
    if (g.processName == "ovlloader") return "menus/diagnostics/name_overlays"_i18n;

    const u64 tid = std::strtoull(g.programId.c_str(), nullptr, 16);
    auto control = std::make_unique<NsApplicationControlData>();
    u64 size = 0;
    NacpLanguageEntry* entry = nullptr;
    if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, tid, control.get(), sizeof(NsApplicationControlData), &size)) &&
        size >= sizeof(control->nacp) && util::descomprimirNombres(&control->nacp) && R_SUCCEEDED(nacpGetLanguageEntry(&control->nacp, &entry)) && entry && entry->name[0])
        return entry->name;

    if (!g.module.empty()) return g.module;
    return g.programId;
}

DiagnosticsPage::DiagnosticsPage() : brls::AppletFrame(true, true)
{
    this->list = new brls::List();
    this->build();
    this->setContentView(this->list);
}

void DiagnosticsPage::build()
{
    const auto grupos = crash::group(crash::readAll(std::vector<std::string>{CRASH_REPORTS_PATH, FATAL_REPORTS_PATH}));

    // Arriba de todo: mandar todo esto (y lo demas del diagnostico) a soporte.
    brls::ListItem* enviar = new brls::ListItem("menus/nexus/diagnostico"_i18n, "menus/nexus/diagnostico_sub"_i18n);
    enviar->getClickEvent()->subscribe([](brls::View*) {
        abrirEnviarSoporte("OQB Updater", NexusTab::diagnosticoSoporte, NexusTab::diagnosticoIncluye());
    });
    this->list->addView(enviar);

    if (grupos.empty()) {
        this->list->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/diagnostics/none"_i18n, true));
        return;
    }

    int total = 0;
    for (const auto& g : grupos) total += g.count;
    const double mb = (crash::totalSize(CRASH_REPORTS_PATH) + crash::totalSize(FATAL_REPORTS_PATH)) / (1024.0 * 1024.0);
    this->list->addView(new brls::Label(brls::LabelStyle::DESCRIPTION,
                                        fmt::format("menus/diagnostics/summary"_i18n, total, grupos.size(), mb), true));

    for (const auto& g : grupos) {
        /* Lo que paso va en el valor y en la descripcion: veces, ultima fecha y
           codigo. La causa, si se conoce, al final, que es lo que el cliente
           puede hacer algo con ello. */
        std::string detalle = fmt::format("menus/diagnostics/detail"_i18n, crash::date(g.lastTimestamp),
                                          g.lastResult.empty() ? "?" : g.lastResult);
        const std::string causa = crash::hint(g);
        if (!causa.empty()) detalle += "\n" + causa;

        brls::ListItem* item = new brls::ListItem(crashProgramName(g), detalle);
        item->setValue(g.count == 1 ? "menus/diagnostics/once"_i18n : fmt::format("menus/diagnostics/times"_i18n, g.count));
        this->list->addView(item);
    }

    brls::ListItem* borrar = new brls::ListItem("menus/diagnostics/clear"_i18n, "menus/diagnostics/clear_desc"_i18n);
    borrar->getClickEvent()->subscribe([this](brls::View* view) {
        brls::Dialog* dialog = new brls::Dialog("menus/diagnostics/clear_confirm"_i18n);
        dialog->addButton("menus/common/yes"_i18n, [this, dialog](brls::View* v) {
            dialog->close([this]() {
                const int n = crash::removeAll(CRASH_REPORTS_PATH) + crash::removeAll(FATAL_REPORTS_PATH);
                brls::Application::notify(fmt::format("menus/diagnostics/cleared"_i18n, n));
                brls::Application::popView();
            });
        });
        dialog->addButton("menus/common/no"_i18n, [dialog](brls::View* v) { dialog->close(); });
        dialog->setCancelable(true);
        dialog->open();
    });
    this->list->addView(borrar);
}
