#include "color_picker_page.hpp"

#include <switch.h>

#include <algorithm>

#include "color_swapper.hpp"
#include "utils.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {
    /* Las claves, no los textos. Resolverlas aqui, en un array de ambito de
       fichero, las dejaria traducidas antes de que main cargue el idioma: el
       resultado serian las propias claves en pantalla. Se resuelven al usarlas. */
    const char* JC_SLOT_KEYS[4] = {
        "menus/color_picker/jc_left_body",
        "menus/color_picker/jc_left_buttons",
        "menus/color_picker/jc_right_body",
        "menus/color_picker/jc_right_buttons"};

    const char* PC_SLOT_KEYS[4] = {
        "menus/color_picker/pc_body",
        "menus/color_picker/pc_buttons",
        "menus/color_picker/pc_left_grip",
        "menus/color_picker/pc_right_grip"};

    const char* CHANNEL_NAMES[3] = {"R", "G", "B"};

    NVGcolor channelColor(int channel)
    {
        switch (channel) {
            case 0:
                return nvgRGB(229, 57, 53);  // R
            case 1:
                return nvgRGB(67, 160, 71);  // G
            default:
                return nvgRGB(30, 136, 229);  // B
        }
    }

    // Descompone el entero del hardware (0xBBGGRR) en [R, G, B]
    void decompose(u32 hw, int rgb[3])
    {
        rgb[0] = hw & 0xFF;          // R
        rgb[1] = (hw >> 8) & 0xFF;   // G
        rgb[2] = (hw >> 16) & 0xFF;  // B
    }

    /* Las formas estan trazadas sobre un lienzo de 320x240, sacadas de los SVG
       de los mandos reales en vez de dibujadas a mano: por eso las curvas son
       Bezier y no rectangulos redondeados. Esto las encaja en el hueco que haya,
       centradas y sin deformar. */
    struct Lienzo
    {
        float s, ox, oy;
        float X(float v) const { return ox + v * s; }
        float Y(float v) const { return oy + v * s; }
        float R(float v) const { return v * s; }
    };

    Lienzo encajar(int x, int y, unsigned width, int alto)
    {
        constexpr float DISENO_W = 320.0f, DISENO_H = 240.0f;
        const float s = std::min((float)width / DISENO_W, (float)alto / DISENO_H);
        return {s, x + ((float)width - DISENO_W * s) / 2.0f, y + ((float)alto - DISENO_H * s) / 2.0f};
    }

    void curva(NVGcontext* vg, const Lienzo& l, float x1, float y1, float x2, float y2, float x3, float y3)
    {
        nvgBezierTo(vg, l.X(x1), l.Y(y1), l.X(x2), l.Y(y2), l.X(x3), l.Y(y3));
    }

    // ---- Pro Controller ----
    void proCuerpo(NVGcontext* vg, const Lienzo& l)
    {
        nvgMoveTo(vg, l.X(37.6f), l.Y(84.6f));
        curva(vg, l, 38.0f, 82.5f, 38.5f, 80.5f, 39.0f, 78.5f);
        nvgLineTo(vg, l.X(41.4f), l.Y(68.7f));
        curva(vg, l, 46.0f, 49.3f, 60.8f, 34.4f, 79.9f, 29.8f);
        curva(vg, l, 100.8f, 24.7f, 127.4f, 24.3f, 160.0f, 24.3f);
        curva(vg, l, 192.6f, 24.3f, 219.1f, 24.7f, 240.0f, 29.8f);
        curva(vg, l, 259.2f, 34.4f, 274.0f, 49.3f, 278.6f, 68.7f);
        nvgLineTo(vg, l.X(280.9f), l.Y(78.5f));
        curva(vg, l, 281.4f, 80.5f, 281.9f, 82.5f, 282.4f, 84.6f);
        nvgLineTo(vg, l.X(219.0f), l.Y(160.0f));
        nvgLineTo(vg, l.X(101.0f), l.Y(160.0f));
        nvgClosePath(vg);
    }

    /* El corte entre cuerpo y grip es la diagonal que trae el propio SVG, no un
       corte horizontal: las tres piezas comparten esa arista y encajan sin
       solaparse ni dejar rendija. */
    void proGripIzq(NVGcontext* vg, const Lienzo& l)
    {
        nvgMoveTo(vg, l.X(37.6f), l.Y(84.6f));
        curva(vg, l, 25.3f, 136.0f, 14.6f, 183.8f, 30.3f, 204.9f);
        curva(vg, l, 35.2f, 211.5f, 42.3f, 215.1f, 51.8f, 215.6f);
        curva(vg, l, 51.9f, 215.6f, 52.0f, 215.6f, 52.1f, 215.6f);
        curva(vg, l, 63.5f, 215.6f, 73.6f, 204.5f, 83.0f, 181.7f);
        curva(vg, l, 88.9f, 168.3f, 94.0f, 160.0f, 101.0f, 160.0f);
        nvgClosePath(vg);
    }

    void proGripDer(NVGcontext* vg, const Lienzo& l)
    {
        nvgMoveTo(vg, l.X(219.0f), l.Y(160.0f));
        curva(vg, l, 226.0f, 160.0f, 231.1f, 168.3f, 237.0f, 181.7f);
        curva(vg, l, 246.4f, 204.5f, 256.5f, 215.7f, 267.9f, 215.7f);
        curva(vg, l, 268.0f, 215.7f, 268.1f, 215.7f, 268.2f, 215.7f);
        nvgLineTo(vg, l.X(268.4f), l.Y(215.7f));
        curva(vg, l, 277.7f, 215.1f, 284.9f, 211.5f, 289.7f, 205.0f);
        curva(vg, l, 305.4f, 183.8f, 294.7f, 136.1f, 282.4f, 84.6f);
        nvgClosePath(vg);
    }

    void proBotones(NVGcontext* vg, const Lienzo& l)
    {
        nvgCircle(vg, l.X(85.7f), l.Y(81.1f), l.R(15.6f));    // palanca izquierda
        nvgCircle(vg, l.X(196.5f), l.Y(118.1f), l.R(15.6f));  // palanca derecha
        nvgCircle(vg, l.X(232.0f), l.Y(62.8f), l.R(9.5f));    // ABXY
        nvgCircle(vg, l.X(253.4f), l.Y(81.1f), l.R(9.5f));
        nvgCircle(vg, l.X(232.0f), l.Y(99.7f), l.R(9.5f));
        nvgCircle(vg, l.X(210.6f), l.Y(80.7f), l.R(9.5f));
        nvgRoundedRect(vg, l.X(101.0f), l.Y(113.3f), l.R(35.4f), l.R(10.4f), l.R(1.6f));  // cruceta
        nvgRoundedRect(vg, l.X(113.1f), l.Y(100.4f), l.R(11.2f), l.R(35.4f), l.R(1.6f));
        nvgCircle(vg, l.X(127.1f), l.Y(60.6f), l.R(5.6f));  // menos
        nvgCircle(vg, l.X(192.9f), l.Y(60.6f), l.R(5.6f));  // mas
        nvgCircle(vg, l.X(178.8f), l.Y(81.1f), l.R(5.6f));  // home
        nvgRoundedRect(vg, l.X(137.6f), l.Y(75.5f), l.R(9.2f), l.R(9.2f), l.R(1.6f));  // captura
    }

    // ---- Joy-Con ----
    void jcCuerpoIzq(NVGcontext* vg, const Lienzo& l)
    {
        nvgMoveTo(vg, l.X(122.6f), l.Y(228.0f));
        nvgLineTo(vg, l.X(93.3f), l.Y(228.0f));
        curva(vg, l, 80.5f, 228.0f, 68.4f, 223.1f, 59.0f, 214.3f);
        curva(vg, l, 49.1f, 204.8f, 43.6f, 192.0f, 43.6f, 178.2f);
        nvgLineTo(vg, l.X(43.6f), l.Y(61.8f));
        curva(vg, l, 43.6f, 57.9f, 44.0f, 54.1f, 44.8f, 50.5f);
        curva(vg, l, 50.1f, 27.9f, 70.0f, 12.0f, 93.3f, 12.0f);
        nvgLineTo(vg, l.X(122.6f), l.Y(12.0f));
        nvgClosePath(vg);
    }

    void jcCuerpoDer(NVGcontext* vg, const Lienzo& l)
    {
        nvgMoveTo(vg, l.X(197.4f), l.Y(12.0f));
        nvgLineTo(vg, l.X(226.7f), l.Y(12.0f));
        curva(vg, l, 250.0f, 12.0f, 269.9f, 27.8f, 275.2f, 50.5f);
        curva(vg, l, 276.0f, 54.1f, 276.4f, 57.9f, 276.4f, 61.8f);
        nvgLineTo(vg, l.X(276.4f), l.Y(178.2f));
        curva(vg, l, 276.4f, 192.0f, 270.9f, 204.8f, 261.0f, 214.3f);
        curva(vg, l, 251.6f, 223.1f, 239.5f, 228.0f, 226.7f, 228.0f);
        nvgLineTo(vg, l.X(197.4f), l.Y(228.0f));
        nvgClosePath(vg);
    }

    void jcBotonesIzq(NVGcontext* vg, const Lienzo& l)
    {
        nvgRect(vg, l.X(115.9f), l.Y(12.0f), l.R(6.6f), l.R(216.0f));  // riel
        nvgCircle(vg, l.X(83.7f), l.Y(67.5f), l.R(16.2f));             // palanca
        nvgCircle(vg, l.X(83.7f), l.Y(110.0f), l.R(7.7f));             // cruceta
        nvgCircle(vg, l.X(83.7f), l.Y(141.1f), l.R(7.7f));
        nvgCircle(vg, l.X(67.5f), l.Y(125.7f), l.R(7.7f));
        nvgCircle(vg, l.X(99.9f), l.Y(125.7f), l.R(7.7f));
        nvgRoundedRect(vg, l.X(89.9f), l.Y(158.5f), l.R(11.7f), l.R(11.7f), l.R(1.2f));  // captura
        nvgRect(vg, l.X(98.7f), l.Y(33.5f), l.R(11.7f), l.R(3.3f));                      // menos
    }

    void jcBotonesDer(NVGcontext* vg, const Lienzo& l)
    {
        nvgRect(vg, l.X(197.4f), l.Y(12.0f), l.R(6.6f), l.R(216.0f));  // riel
        nvgCircle(vg, l.X(236.3f), l.Y(125.7f), l.R(16.2f));           // palanca
        nvgCircle(vg, l.X(236.3f), l.Y(51.9f), l.R(7.7f));             // ABXY
        nvgCircle(vg, l.X(236.3f), l.Y(83.1f), l.R(7.7f));
        nvgCircle(vg, l.X(220.1f), l.Y(67.6f), l.R(7.7f));
        nvgCircle(vg, l.X(252.5f), l.Y(67.6f), l.R(7.7f));
        nvgCircle(vg, l.X(224.8f), l.Y(164.9f), l.R(8.8f));            // home
        nvgRect(vg, l.X(209.6f), l.Y(33.5f), l.R(11.7f), l.R(3.3f));   // mas
        nvgRect(vg, l.X(215.1f), l.Y(28.0f), l.R(3.3f), l.R(11.7f));
    }
}  // namespace

ColorPickerPage::ColorPickerPage(Controller type) : type(type)
{
    numSlots = 4;

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
        /* Primero la SPI, que es la unica que trae los grips. Si no se puede
           leer se cae a la via de siempre, que solo da cuerpo y botones, y los
           grips arrancan con el color del cuerpo: es mejor eso que un gris que
           el usuario aplicaria sin querer. */
        hiddbgInitialize();
        hidsysInitialize();
        const bool leidos = PC::readColors(slots);
        hiddbgExit();
        hidsysExit();

        if (leidos) {
            ok = true;
        }
        else {
            HidNpadControllerColor color;
            if (R_SUCCEEDED(hidGetNpadControllerColorSingle(HidNpadIdType_No1, &color))) {
                decompose(color.main, slots[0]);
                decompose(color.sub, slots[1]);
                for (int canal = 0; canal < 3; ++canal) {
                    slots[2][canal] = slots[0][canal];
                    slots[3][canal] = slots[0][canal];
                }
                ok = true;
            }
        }
    }
    if (!ok) {
        for (int i = 0; i < numSlots; i++) {
            slots[i][0] = slots[i][1] = slots[i][2] = 128;
        }
    }

    this->registerAction("menus/color_picker/apply"_i18n, brls::Key::A, [this] {
        this->apply();
        return true;
    });
    if (numSlots > 1) {
        this->registerAction("menus/color_picker/change_part"_i18n, brls::Key::X, [this] {
            this->cycleSlot(1);
            return true;
        });
    }
    this->registerAction("menus/color_picker/step_down"_i18n, brls::Key::L, [this] {
        this->adjust(-16);
        return true;
    });
    this->registerAction("menus/color_picker/step_up"_i18n, brls::Key::R, [this] {
        this->adjust(16);
        return true;
    });
    // D-pad: navegacion de canal y ajuste fino (ocultos del hint bar)
    this->registerAction("", brls::Key::DUP, [this] {
        this->cycleChannel(-1);
        return true; }, true);
    this->registerAction("", brls::Key::DDOWN, [this] {
        this->cycleChannel(1);
        return true; }, true);
    this->registerAction("", brls::Key::DLEFT, [this] {
        this->adjust(-1);
        return true; }, true);
    this->registerAction("", brls::Key::DRIGHT, [this] {
        this->adjust(1);
        return true; }, true);
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
            toHardware(slots[1]),
            toHardware(slots[2]),
            toHardware(slots[3])};
        res = PC::setColor(values);
    }
    hiddbgExit();
    hidsysExit();
    if (res != 0) {
        const std::string controller = (type == Controller::JoyCon)
                                           ? "menus/color_picker/the_joycons"_i18n
                                           : "menus/color_picker/the_pro_controller"_i18n;
        util::showDialogBoxInfo(fmt::format("menus/color_picker/apply_failed"_i18n, controller, res));
    }
}

brls::View* ColorPickerPage::getDefaultFocus()
{
    return this;
}

namespace {
    /* Pinta una parte y, si esta seleccionada, le marca el contorno. El realce
       sigue la forma real en vez de un rectangulo alrededor, que era lo que
       hacia que no se supiera bien que se estaba cambiando. */
    template <typename Forma>
    void pintarParte(NVGcontext* vg, const Lienzo& l, Forma forma, NVGcolor color, bool seleccionada, NVGcolor realce)
    {
        nvgBeginPath(vg);
        forma(vg, l);
        nvgFillColor(vg, color);
        nvgFill(vg);

        if (seleccionada) {
            nvgBeginPath(vg);
            forma(vg, l);
            nvgStrokeColor(vg, realce);
            nvgStrokeWidth(vg, std::max(3.0f, l.R(4.0f)));
            nvgStroke(vg);
        }
    }
}  // namespace

void ColorPickerPage::drawJoyConPreview(NVGcontext* vg, int x, int y, unsigned width, int previewH, brls::FrameContext* ctx)
{
    const Lienzo l = encajar(x, y, width, previewH);
    const NVGcolor realce = a(ctx->theme->highlightColor1);

    const auto color = [this](int parte) {
        return nvgRGB(slots[parte][0], slots[parte][1], slots[parte][2]);
    };

    pintarParte(vg, l, jcCuerpoIzq, color(0), currentSlot == 0, realce);
    pintarParte(vg, l, jcBotonesIzq, color(1), currentSlot == 1, realce);
    pintarParte(vg, l, jcCuerpoDer, color(2), currentSlot == 2, realce);
    pintarParte(vg, l, jcBotonesDer, color(3), currentSlot == 3, realce);
}

void ColorPickerPage::drawProControllerPreview(NVGcontext* vg, int x, int y, unsigned width, int previewH, brls::FrameContext* ctx)
{
    const Lienzo l = encajar(x, y, width, previewH);
    const NVGcolor realce = a(ctx->theme->highlightColor1);

    const auto color = [this](int parte) {
        return nvgRGB(slots[parte][0], slots[parte][1], slots[parte][2]);
    };

    /* Los grips antes que el cuerpo: comparten la arista diagonal, y pintando
       el cuerpo encima el borde queda limpio en vez de dentado. */
    pintarParte(vg, l, proGripIzq, color(2), currentSlot == 2, realce);
    pintarParte(vg, l, proGripDer, color(3), currentSlot == 3, realce);
    pintarParte(vg, l, proCuerpo, color(0), currentSlot == 0, realce);
    pintarParte(vg, l, proBotones, color(1), currentSlot == 1, realce);
}

void ColorPickerPage::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    nvgFontFaceId(vg, ctx->fontStash->regular);

    const int padding = 40;
    const int cx = x + width / 2;

    // ---- Preview del mando ----
    const int previewH = (int)(height * 0.40f);
    if (type == Controller::JoyCon)
        drawJoyConPreview(vg, x, y, width, previewH, ctx);
    else
        drawProControllerPreview(vg, x, y, width, previewH, ctx);

    // ---- Etiqueta de la parte + hex ----
    int* cur = slots[currentSlot];
    const std::string slotName = i18n::getStr(
        (type == Controller::JoyCon) ? JC_SLOT_KEYS[currentSlot] : PC_SLOT_KEYS[currentSlot]);
    char hex[16];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", cur[0], cur[1], cur[2]);

    int labelY = y + previewH + 34;
    nvgFontSize(vg, 26);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, a(ctx->theme->textColor));
    nvgBeginPath(vg);
    nvgText(vg, cx, labelY, slotName.c_str(), nullptr);

    nvgFontSize(vg, 22);
    nvgFillColor(vg, a(ctx->theme->descriptionColor));
    nvgBeginPath(vg);
    nvgText(vg, cx, labelY + 30, hex, nullptr);

    // ---- Sliders R / G / B ----
    int sy = labelY + 64;
    int rowH = 50;
    int barH = 16;
    int barX0 = x + padding + 44;
    int barX1 = x + width - padding - 64;
    int barW = barX1 - barX0;

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
