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
    nvgFillColor(vg, nvgRGB(255, 255, 255));
    nvgRect(vg, ox, oy, side, side);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgFillColor(vg, nvgRGB(0, 0, 0));
    for (int fy = 0; fy < this->size; fy++)
        for (int fx = 0; fx < this->size; fx++)
            if (qrcodegen_getModule(this->qr.data(), fx, fy))
                nvgRect(vg, ox + (fx + margen) * celda, oy + (fy + margen) * celda, celda + 0.5f, celda + 0.5f);
    nvgFill(vg);
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

    this->button->setBoundaries(
        this->x + this->width / 2 - style->CrashFrame.buttonWidth / 2,
        this->y + (this->height - style->CrashFrame.buttonHeight * 2),
        style->CrashFrame.buttonWidth,
        style->CrashFrame.buttonHeight);
    this->button->invalidate();

    start = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(150);
}

DonarPage::~DonarPage()
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
