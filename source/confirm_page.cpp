#include "confirm_page.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

#include "fs.hpp"
#include "main_frame.hpp"
#include "utils.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;
ConfirmPage::ConfirmPage(brls::StagedAppletFrame* frame, const std::string& text)
{
    this->button = (new brls::Button(brls::ButtonStyle::REGULAR))->setLabel("menus/common/back"_i18n);
    this->button->setParent(this);
    this->button->getClickEvent()->subscribe([frame, this](View* view) {
        if (!frame->isLastStage())
            frame->nextStage();
        else if (this->goHome)
            brls::Application::pushView(new MainFrame());
    });

    this->label = new brls::Label(brls::LabelStyle::DIALOG, text, true);
    this->label->setHorizontalAlign(NVG_ALIGN_CENTER);
    this->label->setParent(this);
}

ConfirmPage* ConfirmPage::returnToList()
{
    this->goHome = false;
    this->button->setLabel("menus/common/back_to_list"_i18n);
    this->button->getClickEvent()->subscribe([](View* view) { brls::Application::popView(); });
    return this;
}

ConfirmPage* ConfirmPage::volverDosAtras(const std::string& etiqueta)
{
    this->goHome = false;
    this->button->setLabel(etiqueta);
    // El segundo cierre va en el callback: dos popView seguidos sacarian dos
    // veces la misma vista, porque la primera sigue en la pila mientras se anima.
    this->button->getClickEvent()->subscribe([](View* view) {
        brls::Application::popView(brls::ViewAnimation::FADE, [] { brls::Application::popView(); });
    });
    return this;
}

ConfirmPage_Done::ConfirmPage_Done(brls::StagedAppletFrame* frame, const std::string& text) : ConfirmPage(frame, text)
{
    this->button->setLabel("menus/common/go_home"_i18n);
    this->done = true;
}

ConfirmPage_AppUpdate::ConfirmPage_AppUpdate(brls::StagedAppletFrame* frame, const std::string& text) : ConfirmPage_Done(frame, text)
{
    this->goHome = false;
    this->button->setLabel(util::isForwarder() ? "menus/common/close_app"_i18n : "menus/common/open_new_version"_i18n);
    this->button->getClickEvent()->subscribe([](View* view) {
        if (util::isForwarder()) {
            // Se instala aqui mismo y la app se cierra; se abre de nuevo desde el menu.
            util::installPendingAppUpdate();
            brls::Application::quit();
            return;
        }
        envSetNextLoad(FORWARDER_PATH, FORWARDER_PATH);
        romfsExit();
        brls::Application::quit();
    });
    this->registerAction("", brls::Key::B, [this] { return true; });
}

ConfirmPage_SelfUpdate::ConfirmPage_SelfUpdate(brls::StagedAppletFrame* frame, const std::string& text) : ConfirmPage_Done(frame, text)
{
    this->goHome = false;
    this->button->setLabel("menus/common/ok"_i18n);
    this->button->getClickEvent()->subscribe([](View* view) {
        util::restartApp();
    });
    this->registerAction("", brls::Key::B, [this] { return true; });
}

ConfirmPage_AmsUpdate::ConfirmPage_AmsUpdate(brls::StagedAppletFrame* frame, const std::string& text, bool erista) : ConfirmPage_Done(frame, text)
{
    this->goHome = false;
    this->button->setLabel(erista ? "menus/common/reboot"_i18n : "menus/common/shut_down"_i18n);
    this->button->getClickEvent()->subscribe([this, erista](View* view) {
        if (erista) {
            util::rebootToPayload(RCM_PAYLOAD_PATH);
        }
        else {
            util::prepareMarikoPayload();
            util::shutDown(true);
        }
    });
    this->registerAction("", brls::Key::B, [this] { return true; });
};

ConfirmPage_Deferred::ConfirmPage_Deferred(brls::StagedAppletFrame* frame, std::shared_ptr<std::string> message)
    : ConfirmPage_Done(frame, ""), message(std::move(message))
{
}

void ConfirmPage_Deferred::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    if (!this->applied) {
        this->applied = true;
        this->label->setText(*this->message);
        this->invalidate();
    }
    ConfirmPage_Done::draw(vg, x, y, width, height, style, ctx);
}

void ConfirmPage::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    if (!this->done) {
        auto end = std::chrono::high_resolution_clock::now();
        auto missing = std::max(1l - std::chrono::duration_cast<std::chrono::seconds>(end - start).count(), 0l);
        auto text = std::string("menus/common/continue"_i18n);
        if (missing > 0) {
            this->button->setLabel(fmt::format("{} ({})", text, missing));
            this->button->setState(brls::ButtonState::DISABLED);
        }
        else {
            this->button->setLabel(text);
            this->button->setState(brls::ButtonState::ENABLED);
        }
        this->button->invalidate();
    }
    else {
        this->button->setState(brls::ButtonState::ENABLED);
    }
    this->label->frame(ctx);
    this->button->frame(ctx);
}

brls::View* ConfirmPage::getDefaultFocus()
{
    return this->button;
}

void ConfirmPage::layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash)
{
    /* Margen a los lados: a todo el ancho el texto llegaba casi al borde de la
       pantalla y costaba leerlo. */
    constexpr unsigned MARGEN = 120;
    this->label->setWidth(this->width > MARGEN * 3 ? this->width - MARGEN * 2 : this->width);
    this->label->invalidate(true);
    // this->label->setBackground(brls::ViewBackground::DEBUG);
    /* Centrado en el espacio que queda sobre el boton, pero nunca por encima del
       borde: un texto largo (la explicacion de la instalacion limpia) quedaba con
       la y negativa, fuera de la vista, y la pantalla se veia vacia. */
    const int zona = (int)this->height - (int)style->CrashFrame.buttonHeight * 3 - 20;
    const int arriba = std::max(24, (zona - (int)this->label->getHeight()) / 2);
    this->label->setBoundaries(
        this->x + this->width / 2 - this->label->getWidth() / 2,
        this->y + arriba,
        this->label->getWidth(),
        this->label->getHeight());

    this->button->setBoundaries(
        this->x + this->width / 2 - style->CrashFrame.buttonWidth / 2,
        this->y + (this->height - style->CrashFrame.buttonHeight * 3),
        style->CrashFrame.buttonWidth,
        style->CrashFrame.buttonHeight);
    this->button->invalidate();

    start = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(150);
}

ConfirmPage::~ConfirmPage()
{
    delete this->label;
    delete this->button;
}