#include "protection_page.hpp"

#include "protection.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

ProtectionPage::ProtectionPage() : brls::AppletFrame(true, true)
{
    this->list = new brls::List();

    const auto checks = protection::run();

    for (const auto& check : checks) {
        /* El motivo va de descripcion: en una sola linea no cabe y lo que
           importa cuando algo falla es justamente por que fallo. */
        brls::ListItem* item = new brls::ListItem(check.name, check.detail);

        switch (check.status) {
            case protection::Status::Ok:
                item->setValue("menus/protection/ok"_i18n, true);
                break;
            case protection::Status::Warning:
                item->setValue("menus/protection/warning"_i18n);
                break;
            case protection::Status::Fail:
                item->setValue("menus/protection/fail"_i18n);
                break;
        }

        this->list->addView(item);
    }

    this->list->addView(new brls::Label(
        brls::LabelStyle::DESCRIPTION,
        protection::anyFailed(checks) ? "menus/protection/summary_fail"_i18n
                                      : "menus/protection/summary_ok"_i18n,
        true));

    this->setContentView(this->list);
}
