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
#include "cliente.hpp"
#include "cliente_page.hpp"
#include "donar_page.hpp"
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

AboutTab::AboutTab(const std::string& tag, const std::string& donationUrl, const std::vector<protection::Check>& checks) : brls::List()
{
    this->addStatusPanel(tag, checks);
    this->addSupport(donationUrl);
    this->addAppInfo();
}

namespace {

    /* El QR a la izquierda y el texto a su derecha, en una sola fila con su
       propio margen arriba y abajo: separados, el QR quedaba encima del
       encabezado. Puede tener el foco, para que la lista baje hasta ella. */
    class ApoyoView : public brls::View
    {
    private:
        QrCodeView* qr;
        std::string texto;

    public:
        explicit ApoyoView(const std::string& url) : qr(new QrCodeView(url)), texto("menus/donar/support_text"_i18n)
        {
            this->qr->setParent(this);
            this->setHeight(260);
        }

        ~ApoyoView()
        {
            delete this->qr;
        }

        brls::View* getDefaultFocus() override
        {
            return this;
        }

        void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override
        {
            const int margen = 24;
            const int lado = this->height - margen * 2;
            this->qr->setBoundaries(this->x + margen, this->y + margen, lado, lado);
        }

        void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override
        {
            this->qr->frame(ctx);
            const int margen = 24;
            const int lado = height - margen * 2;
            const float tx = x + margen + lado + 36;
            nvgFontFaceId(vg, ctx->fontStash->regular);
            nvgFontSize(vg, style->Label.regularFontSize);
            nvgTextLineHeight(vg, 1.4f);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgFillColor(vg, this->a(ctx->theme->textColor));
            float caja[4];
            nvgTextBoxBounds(vg, 0, 0, width - (tx - x) - margen, this->texto.c_str(), nullptr, caja);
            const float alto = caja[3] - caja[1];
            nvgBeginPath(vg);
            nvgTextBox(vg, tx, y + (height - alto) / 2.0f, width - (tx - x) - margen, this->texto.c_str(), nullptr);
        }
    };

}  // namespace

void AboutTab::addSupport(const std::string& donationUrl)
{
    if (donationUrl.empty())
        return;
    this->addView(new brls::Header("menus/donar/support_title"_i18n));
    this->addView(new ApoyoView(donationUrl));
}

void AboutTab::addStatusPanel(const std::string& tag, const std::vector<protection::Check>& checks)
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
    const bool failed = protection::anyFailed(checks);
    const size_t failures = std::count_if(checks.begin(), checks.end(),
                                          [](const protection::Check& c) {
                                              return c.status == protection::Status::Fail;
                                          });

    const bool nextendo = protection::failedOnlyByNextendo(checks);
    brls::ListItem* prot = row("menus/about/status_protection"_i18n,
                               nextendo ? "menus/about/status_protection_nextendo"_i18n
                               : failed ? "menus/about/status_protection_fail"_i18n
                                        : "menus/about/status_protection_ok"_i18n);
    // Una comprobacion y varias no se dicen igual, asi que son dos textos en vez
    // de uno con un numero encajado a la fuerza.
    prot->setSubLabel(!failed   ? "menus/about/status_protection_ok_sub"_i18n
                      : nextendo ? (protection::nextendoOnSysnand(checks)
                                        ? "menus/about/status_protection_nextendo_sysnand_sub"_i18n
                                    : protection::nextendoIncomplete(checks)
                                        ? "menus/about/status_protection_nextendo_incomplete_sub"_i18n
                                        : "menus/about/status_protection_nextendo_sub"_i18n)
                      : failures == 1
                          ? "menus/about/status_protection_fail_one"_i18n
                          : fmt::format("menus/about/status_protection_fail_many"_i18n, failures));
    prot->getClickEvent()->subscribe([](brls::View* view) {
        brls::PopupFrame::open("menus/tools/protection"_i18n, new ProtectionPage(),
                               "menus/tools/protection_desc"_i18n, "");
    });
    this->addView(prot);

    /* Cliente OQB. A cualquiera se le muestra como registrarse; el codigo de
       soporte solo aparece con un certificado firmado por OQB para esta
       consola. */
    const cliente::Estado estado = cliente::cargar();
    brls::ListItem* clienteRow = row("menus/cliente/titulo"_i18n,
                                     estado.valido ? "menus/cliente/registrado"_i18n : "menus/cliente/no_registrado"_i18n,
                                     !estado.valido);
    clienteRow->setSubLabel(estado.valido ? fmt::format("menus/cliente/registrado_sub"_i18n, estado.nombre)
                                          : "menus/cliente/no_registrado_sub"_i18n);
    clienteRow->getClickEvent()->subscribe([clienteRow](brls::View* view) {
        brls::PopupFrame::open("menus/cliente/titulo"_i18n, new ClientePage([clienteRow](const cliente::Estado& e) {
                                   clienteRow->setValue("menus/cliente/registrado"_i18n);
                                   clienteRow->setSubLabel(fmt::format("menus/cliente/registrado_sub"_i18n, e.nombre));
                               }),
                               "", "");
    });
    this->addView(clienteRow);
}

namespace {

    /* Texto que puede tener el foco: es lo ultimo de la lista, y sin foco
       borealis no baja hasta el y el descargo queda fuera de la pantalla. */
    class LabelAlcanzable : public brls::Label
    {
    public:
        using brls::Label::Label;
        brls::View* getDefaultFocus() override
        {
            return this;
        }
    };

}  // namespace

void AboutTab::addAppInfo()
{
    this->addView(new brls::Header("menus/about/disclaimers_title"_i18n));

    brls::Label* subTitle = new brls::Label(
        brls::LabelStyle::REGULAR,
        "menus/about/title"_i18n + "\nOH! QUÉ BACÁN",
        true);
    subTitle->setHorizontalAlign(NVG_ALIGN_CENTER);
    this->addView(subTitle);

    brls::Label* links = new LabelAlcanzable(
        brls::LabelStyle::SMALL,
        "menus/about/copyright"_i18n + "\n" + "menus/about/disclaimers"_i18n,
        true);
    this->addView(links);
}
