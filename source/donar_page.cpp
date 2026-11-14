#include "donar_page.hpp"

#include <algorithm>

#include "constants.hpp"
#include "fs.hpp"
#include "qrcodegen.h"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {

    // Segundos antes de poder cerrarla.
    constexpr long ESPERA = 4;

    constexpr const char APERTURAS_PATH[] = "/config/aio-switch-updater/aperturas.json";

}  // namespace

QrCodeView::QrCodeView(const std::string& text) : qr(qrcodegen_BUFFER_LEN_MAX)
{
    std::vector<uint8_t> temp(qrcodegen_BUFFER_LEN_MAX);
    if (qrcodegen_encodeText(text.c_str(), temp.data(), this->qr.data(), qrcodegen_Ecc_LOW,
                             qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
        this->size = qrcodegen_getSize(this->qr.data());
}

void QrCodeView::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    if (this->size <= 0)
        return;
    const int margen = 4;
    const int side = std::min(width, height);
    const float celda = static_cast<float>(side) / (this->size + 2 * margen);
    const float ox = x + (static_cast<int>(width) - side) / 2.f, oy = y + (static_cast<int>(height) - side) / 2.f;
    nvgBeginPath(vg);
    // Con this->a(): el QR se desvanece junto con su ventana (antes quedaba solido al abrir y cerrar).
    nvgFillColor(vg, this->a(nvgRGB(255, 255, 255)));
    nvgRect(vg, ox, oy, side, side);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgFillColor(vg, this->a(nvgRGB(0, 0, 0)));
    for (int fy = 0; fy < this->size; fy++)
        for (int fx = 0; fx < this->size; fx++)
            if (qrcodegen_getModule(this->qr.data(), fx, fy))
                nvgRect(vg, ox + (fx + margen) * celda, oy + (fy + margen) * celda, celda + 0.5f, celda + 0.5f);
    nvgFill(vg);
}

QrFilaView::QrFilaView(const std::string& url, const std::string& texto) : qr(new QrCodeView(url)), texto(texto)
{
    this->qr->setParent(this);
    this->setHeight(280);
}

QrFilaView::~QrFilaView()
{
    delete this->qr;
}

void QrFilaView::layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash)
{
    const int margen = 20;
    const int lado = this->height - margen * 2;
    this->qr->setBoundaries(this->x + margen, this->y + margen, lado, lado);
}

void QrFilaView::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    this->qr->frame(ctx);
    const int margen = 20;
    const int lado = height - margen * 2;
    const float tx = x + margen + lado + 36;
    const float ancho = width - (tx - x) - margen;
    nvgFontFaceId(vg, ctx->fontStash->regular);
    nvgFontSize(vg, style->Label.regularFontSize);
    nvgTextLineHeight(vg, 1.4f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, this->a(ctx->theme->textColor));
    float caja[4];
    nvgTextBoxBounds(vg, 0, 0, ancho, this->texto.c_str(), nullptr, caja);
    nvgBeginPath(vg);
    nvgTextBox(vg, tx, y + (height - (caja[3] - caja[1])) / 2.0f, ancho, this->texto.c_str(), nullptr);
}

DonarPage::DonarPage(const std::string& url)
{
    this->qr = new QrCodeView(url);
    this->qr->setParent(this);

    this->title = new brls::Label(brls::LabelStyle::DIALOG, "menus/donar/title"_i18n, true);
    this->title->setHorizontalAlign(NVG_ALIGN_LEFT);
    this->title->setParent(this);

    this->label = new brls::Label(brls::LabelStyle::REGULAR, "menus/donar/text"_i18n, true);
    this->label->setHorizontalAlign(NVG_ALIGN_LEFT);
    this->label->setParent(this);

    this->button = (new brls::Button(brls::ButtonStyle::REGULAR))->setLabel("menus/donar/later"_i18n);
    this->button->setParent(this);
    this->button->getClickEvent()->subscribe([](View* view) { brls::Application::popView(); });

    // Solo se cierra con el boton, cuando termina la cuenta.
    this->registerAction("", brls::Key::B, [] { return true; });
}

void DonarPage::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    const auto now = std::chrono::high_resolution_clock::now();
    const long missing = std::max(ESPERA - std::chrono::duration_cast<std::chrono::seconds>(now - start).count(), 0l);
    const std::string text = "menus/donar/later"_i18n;
    if (missing > 0) {
        this->button->setLabel(fmt::format("{} ({})", text, missing));
        this->button->setState(brls::ButtonState::DISABLED);
    }
    else {
        this->button->setLabel(text);
        this->button->setState(brls::ButtonState::ENABLED);
    }
    this->button->invalidate();

    // Fondo atenuado y el panel, con los colores de los dialogos del sistema.
    nvgBeginPath(vg);
    nvgFillColor(vg, this->a(ctx->theme->dialogBackdrop));
    nvgRect(vg, x, y, width, height);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgFillColor(vg, this->a(ctx->theme->dialogColor));
    nvgRoundedRect(vg, this->panelX, this->panelY, this->panelW, this->panelH, style->Dialog.cornerRadius);
    nvgFill(vg);

    this->qr->frame(ctx);
    this->title->frame(ctx);
    this->label->frame(ctx);
    this->button->frame(ctx);
}

brls::View* DonarPage::getDefaultFocus()
{
    return this->button;
}

void DonarPage::layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash)
{
    // El QR a la izquierda y el texto a su derecha, centrados en la pantalla.
    const int qrSide = std::min<int>(this->height * 0.5f, 340);
    const int gap = 60;
    const int textWidth = std::min<int>(this->width * 0.45f, 560);
    const int left = this->x + (this->width - (qrSide + gap + textWidth)) / 2;
    const int area = this->height - style->CrashFrame.buttonHeight * 3;

    this->title->setWidth(textWidth);
    this->title->invalidate(true);
    this->label->setWidth(textWidth);
    this->label->invalidate(true);
    const int textHeight = this->title->getHeight() + 24 + this->label->getHeight();
    const int top = this->y + (area - std::max(textHeight, qrSide)) / 2;

    this->qr->setBoundaries(left, top + (std::max(textHeight, qrSide) - qrSide) / 2, qrSide, qrSide);

    const int textTop = top + (std::max(textHeight, qrSide) - textHeight) / 2;
    this->title->setBoundaries(left + qrSide + gap, textTop, textWidth, this->title->getHeight());
    this->label->setBoundaries(left + qrSide + gap, textTop + this->title->getHeight() + 24, textWidth, this->label->getHeight());

    // El boton, debajo del contenido; y el panel, alrededor de todo.
    const int margen = 40;
    const int contenidoAlto = std::max(textHeight, qrSide);
    const int botonY = top + contenidoAlto + margen;
    this->button->setBoundaries(
        this->x + this->width / 2 - style->CrashFrame.buttonWidth / 2,
        botonY,
        style->CrashFrame.buttonWidth,
        style->CrashFrame.buttonHeight);
    this->button->invalidate();

    this->panelX = left - margen;
    this->panelY = top - margen;
    this->panelW = qrSide + gap + textWidth + margen * 2;
    this->panelH = (botonY + style->CrashFrame.buttonHeight + margen) - this->panelY;

    start = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(150);
}

DonarPage::~DonarPage()
{
    delete this->qr;
    delete this->title;
    delete this->label;
    delete this->button;
}

QrStagePage::QrStagePage(brls::StagedAppletFrame* frame, const std::string& title, const std::string& text, const std::string& url) : frame(frame)
{
    this->qr = new QrCodeView(url);
    this->qr->setParent(this);
    this->title = new brls::Label(brls::LabelStyle::DIALOG, title, true);
    this->title->setHorizontalAlign(NVG_ALIGN_LEFT);
    this->title->setParent(this);
    this->label = new brls::Label(brls::LabelStyle::REGULAR, text, true);
    this->label->setHorizontalAlign(NVG_ALIGN_LEFT);
    this->label->setParent(this);
    this->button = (new brls::Button(brls::ButtonStyle::PRIMARY))->setLabel("menus/common/continue"_i18n);
    this->button->setParent(this);
    this->button->getClickEvent()->subscribe([this](View* view) {
        if (!this->frame->isLastStage())
            this->frame->nextStage();
    });
}

void QrStagePage::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    this->qr->frame(ctx);
    this->title->frame(ctx);
    this->label->frame(ctx);
    this->button->frame(ctx);
}

brls::View* QrStagePage::getDefaultFocus()
{
    return this->button;
}

void QrStagePage::layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash)
{
    // Igual que la invitacion a donar: QR a la izquierda, texto a la derecha.
    const int qrSide = std::min<int>(this->height * 0.5f, 300);
    const int gap = 50;
    const int textWidth = std::min<int>(this->width * 0.5f, 560);
    const int left = this->x + (this->width - (qrSide + gap + textWidth)) / 2;
    const int area = this->height - style->CrashFrame.buttonHeight * 3;

    this->title->setWidth(textWidth);
    this->title->invalidate(true);
    this->label->setWidth(textWidth);
    this->label->invalidate(true);
    const int textHeight = this->title->getHeight() + 24 + this->label->getHeight();
    const int alto = std::max(textHeight, qrSide);
    const int top = this->y + (area - alto) / 2;

    this->qr->setBoundaries(left, top + (alto - qrSide) / 2, qrSide, qrSide);
    const int textTop = top + (alto - textHeight) / 2;
    this->title->setBoundaries(left + qrSide + gap, textTop, textWidth, this->title->getHeight());
    this->label->setBoundaries(left + qrSide + gap, textTop + this->title->getHeight() + 24, textWidth, this->label->getHeight());

    this->button->setBoundaries(
        this->x + this->width / 2 - style->CrashFrame.buttonWidth / 2,
        this->y + (this->height - style->CrashFrame.buttonHeight * 2),
        style->CrashFrame.buttonWidth,
        style->CrashFrame.buttonHeight);
    this->button->invalidate();
}

QrStagePage::~QrStagePage()
{
    delete this->qr;
    delete this->title;
    delete this->label;
    delete this->button;
}

namespace donar {

    void showIfDue(const std::string& url)
    {
        nlohmann::ordered_json registro = fs::parseJsonFile(APERTURAS_PATH);
        if (!registro.is_object())
            registro = nlohmann::ordered_json::object();
        const long veces = (registro.contains("aperturas") && registro["aperturas"].is_number_integer() ? registro["aperturas"].get<long>() : 0) + 1;
        registro["aperturas"] = veces;
        fs::writeJsonToFile(registro, APERTURAS_PATH);

        if (url.empty() || veces < 5 || (veces - 5) % 10 != 0)
            return;
        brls::Application::pushView(new DonarPage(url));
    }

}  // namespace donar
