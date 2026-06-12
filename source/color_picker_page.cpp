#include "color_picker_page.hpp"

#include <switch.h>

#include "color_swapper.hpp"
#include "utils.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {
    const char* JC_SLOT_NAMES[4] = {
        "Joy-Con Izq · Cuerpo",
        "Joy-Con Izq · Botones",
        "Joy-Con Der · Cuerpo",
        "Joy-Con Der · Botones"};

    const char* PC_SLOT_NAMES[2] = {
        "Pro Controller · Cuerpo",
        "Pro Controller · Botones"};

    const char* CHANNEL_NAMES[3] = {"R", "G", "B"};

    NVGcolor channelColor(int channel)
    {
        switch (channel) {
            case 0: return nvgRGB(229, 57, 53);   // R
            case 1: return nvgRGB(67, 160, 71);    // G
            default: return nvgRGB(30, 136, 229);  // B
        }
    }

    // Descompone el entero del hardware (0xBBGGRR) en [R, G, B]
    void decompose(u32 hw, int rgb[3])
    {
        rgb[0] = hw & 0xFF;          // R
        rgb[1] = (hw >> 8) & 0xFF;   // G
        rgb[2] = (hw >> 16) & 0xFF;  // B
    }
}  // namespace

ColorPickerPage::ColorPickerPage(Controller type) : type(type)
{
    numSlots = (type == Controller::JoyCon) ? 4 : 2;

    // Inicializar con los colores actuales del mando
    bool ok = false;
    if (type == Controller::JoyCon) {
        HidNpadControllerColor left, right;
        if (R_SUCCEEDED(hidGetNpadControllerColorSplit(HidNpadIdType_Handheld, &left, &right))) {
            decompose(left.main, slots[0]);
            decompose(left.sub, slots[1]);
            decompose(right.main, slots[2]);
            decompose(right.sub, slots[3]);
            ok = true;
        }
    }
    else {
        HidNpadControllerColor color;
        if (R_SUCCEEDED(hidGetNpadControllerColorSingle(HidNpadIdType_No1, &color))) {
            decompose(color.main, slots[0]);
            decompose(color.sub, slots[1]);
            ok = true;
        }
    }
    if (!ok) {
        for (int i = 0; i < numSlots; i++) {
            slots[i][0] = slots[i][1] = slots[i][2] = 128;
        }
    }

    this->registerAction("Aplicar", brls::Key::A, [this] {
        this->apply();
        return true;
    });
    if (numSlots > 1) {
        this->registerAction("Cambiar parte", brls::Key::X, [this] {
            this->cycleSlot(1);
            return true;
        });
    }
    this->registerAction("Ajuste rápido (-16)", brls::Key::L, [this] {
        this->adjust(-16);
        return true;
    });
    this->registerAction("Ajuste rápido (+16)", brls::Key::R, [this] {
        this->adjust(16);
        return true;
    });
    // D-pad: navegacion de canal y ajuste fino (ocultos del hint bar)
    this->registerAction("", brls::Key::DUP, [this] {
        this->cycleChannel(-1);
        return true;
    }, true);
    this->registerAction("", brls::Key::DDOWN, [this] {
        this->cycleChannel(1);
        return true;
    }, true);
    this->registerAction("", brls::Key::DLEFT, [this] {
        this->adjust(-1);
        return true;
    }, true);
    this->registerAction("", brls::Key::DRIGHT, [this] {
        this->adjust(1);
        return true;
    }, true);
}

int ColorPickerPage::toHardware(const int rgb[3])
{
    return (rgb[2] << 16) | (rgb[1] << 8) | rgb[0];
}

void ColorPickerPage::cycleSlot(int dir)
{
    currentSlot = (currentSlot + dir + numSlots) % numSlots;
}

void ColorPickerPage::cycleChannel(int dir)
{
    currentChannel = (currentChannel + dir + 3) % 3;
}

void ColorPickerPage::adjust(int delta)
{
    int& v = slots[currentSlot][currentChannel];
    v += delta;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
}

void ColorPickerPage::apply()
{
    hiddbgInitialize();
    hidsysInitialize();
    int res;
    if (type == Controller::JoyCon) {
        std::vector<int> values = {
            toHardware(slots[0]),
            toHardware(slots[1]),
            toHardware(slots[2]),
            toHardware(slots[3])};
        res = JC::setColor(values);
    }
    else {
        std::vector<int> values = {
            toHardware(slots[0]),
            toHardware(slots[1])};
        res = PC::setColor(values);
    }
    hiddbgExit();
    hidsysExit();
    if (res != 0) {
        std::string mando = (type == Controller::JoyCon) ? "los Joy-Cons" : "el Pro Controller";
        util::showDialogBoxInfo("No se pudo aplicar el color. Asegúrate de que " + mando + " esté(n) conectado(s) y vuelve a intentarlo.\nError: " + std::to_string(res));
    }
}

brls::View* ColorPickerPage::getDefaultFocus()
{
    return this;
}

void ColorPickerPage::drawJoyConPreview(NVGcontext* vg, int x, int y, unsigned width, int previewH, brls::FrameContext* ctx)
{
    const int cx    = x + width / 2;
    const int jcW   = 96;
    const int jcGap = 40;
    const int jcY   = y + 20;
    const int jcH   = previewH - 20;

    int jcX[2] = {cx - jcGap / 2 - jcW, cx + jcGap / 2};

    for (int side = 0; side < 2; side++) {
        int bodySlot = side == 0 ? 0 : 2;
        int btnSlot  = side == 0 ? 1 : 3;

        NVGcolor bodyCol = nvgRGB(slots[bodySlot][0], slots[bodySlot][1], slots[bodySlot][2]);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, jcX[side], jcY, jcW, jcH, 18);
        nvgFillColor(vg, bodyCol);
        nvgFill(vg);

        NVGcolor btnCol = nvgRGB(slots[btnSlot][0], slots[btnSlot][1], slots[btnSlot][2]);
        int btnW = jcW / 2;
        int btnH = jcH / 3;
        int btnX = jcX[side] + (jcW - btnW) / 2;
        int btnY = jcY + (side == 0 ? jcH / 6 : jcH - btnH - jcH / 6);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, btnX, btnY, btnW, btnH, 10);
        nvgFillColor(vg, btnCol);
        nvgFill(vg);

        if (currentSlot == bodySlot) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, jcX[side] - 4, jcY - 4, jcW + 8, jcH + 8, 22);
            nvgStrokeColor(vg, a(ctx->theme->highlightColor1));
            nvgStrokeWidth(vg, 4);
            nvgStroke(vg);
        }
        if (currentSlot == btnSlot) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, btnX - 4, btnY - 4, btnW + 8, btnH + 8, 14);
            nvgStrokeColor(vg, a(ctx->theme->highlightColor1));
            nvgStrokeWidth(vg, 4);
            nvgStroke(vg);
        }
    }
}

void ColorPickerPage::drawProControllerPreview(NVGcontext* vg, int x, int y, unsigned width, int previewH, brls::FrameContext* ctx)
{
    const int cx    = x + width / 2;
    const int bodyW = 220;
    const int bodyH = previewH - 30;
    const int bodyX = cx - bodyW / 2;
    const int bodyY = y + 20;

    // Cuerpo del mando
    NVGcolor bodyCol = nvgRGB(slots[0][0], slots[0][1], slots[0][2]);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, bodyX, bodyY, bodyW, bodyH, 40);
    nvgFillColor(vg, bodyCol);
    nvgFill(vg);

    // Botones (zona derecha del mando: 4 circulos)
    NVGcolor btnCol = nvgRGB(slots[1][0], slots[1][1], slots[1][2]);
    int btnR  = 13;
    int clusterX = bodyX + bodyW - 56;
    int clusterY = bodyY + bodyH / 2 - 6;
    int offs[4][2] = {{0, -btnR - 6}, {0, btnR + 6}, {-btnR - 6, 0}, {btnR + 6, 0}};
    for (auto& o : offs) {
        nvgBeginPath(vg);
        nvgCircle(vg, clusterX + o[0], clusterY + o[1], btnR);
        nvgFillColor(vg, btnCol);
        nvgFill(vg);
    }

    if (currentSlot == 0) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, bodyX - 4, bodyY - 4, bodyW + 8, bodyH + 8, 44);
        nvgStrokeColor(vg, a(ctx->theme->highlightColor1));
        nvgStrokeWidth(vg, 4);
        nvgStroke(vg);
    }
    if (currentSlot == 1) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, clusterX - btnR * 2 - 10, clusterY - btnR * 2 - 10, btnR * 4 + 20, btnR * 4 + 20, 16);
        nvgStrokeColor(vg, a(ctx->theme->highlightColor1));
        nvgStrokeWidth(vg, 4);
        nvgStroke(vg);
    }
}

void ColorPickerPage::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    nvgFontFaceId(vg, ctx->fontStash->regular);

    const int padding = 40;
    const int cx      = x + width / 2;

    // ---- Preview del mando ----
    const int previewH = (int)(height * 0.40f);
    if (type == Controller::JoyCon)
        drawJoyConPreview(vg, x, y, width, previewH, ctx);
    else
        drawProControllerPreview(vg, x, y, width, previewH, ctx);

    // ---- Etiqueta de la parte + hex ----
    int* cur = slots[currentSlot];
    const char* slotName = (type == Controller::JoyCon) ? JC_SLOT_NAMES[currentSlot] : PC_SLOT_NAMES[currentSlot];
    char hex[16];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", cur[0], cur[1], cur[2]);

    int labelY = y + previewH + 34;
    nvgFontSize(vg, 26);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, a(ctx->theme->textColor));
    nvgBeginPath(vg);
    nvgText(vg, cx, labelY, slotName, nullptr);

    nvgFontSize(vg, 22);
    nvgFillColor(vg, a(ctx->theme->descriptionColor));
    nvgBeginPath(vg);
    nvgText(vg, cx, labelY + 30, hex, nullptr);

    // ---- Sliders R / G / B ----
    int sy    = labelY + 64;
    int rowH  = 50;
    int barH  = 16;
    int barX0 = x + padding + 44;
    int barX1 = x + width - padding - 64;
    int barW  = barX1 - barX0;

    for (int ch = 0; ch < 3; ch++) {
        int rowY = sy + ch * rowH;
        int barY = rowY + (rowH - barH) / 2;
        bool sel = ch == currentChannel;

        if (sel) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x + padding - 10, rowY, width - padding * 2 + 20, rowH, 8);
            nvgFillColor(vg, a(ctx->theme->highlightBackgroundColor));
            nvgFill(vg);
        }

        nvgFontSize(vg, 24);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, a(ctx->theme->textColor));
        nvgBeginPath(vg);
        nvgText(vg, x + padding, rowY + rowH / 2, CHANNEL_NAMES[ch], nullptr);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, barX0, barY, barW, barH, barH / 2);
        nvgFillColor(vg, a(ctx->theme->listItemSeparatorColor));
        nvgFill(vg);

        float frac = cur[ch] / 255.0f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, barX0, barY, (float)barW * frac, barH, barH / 2);
        nvgFillColor(vg, channelColor(ch));
        nvgFill(vg);

        int knobX = barX0 + (int)((float)barW * frac);
        nvgBeginPath(vg);
        nvgCircle(vg, knobX, barY + barH / 2, sel ? 14 : 10);
        nvgFillColor(vg, nvgRGB(255, 255, 255));
        nvgFill(vg);
        nvgStrokeColor(vg, channelColor(ch));
        nvgStrokeWidth(vg, 3);
        nvgStroke(vg);

        char val[8];
        snprintf(val, sizeof(val), "%d", cur[ch]);
        nvgFontSize(vg, 24);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, a(ctx->theme->textColor));
        nvgBeginPath(vg);
        nvgText(vg, x + width - padding, rowY + rowH / 2, val, nullptr);
    }
}
