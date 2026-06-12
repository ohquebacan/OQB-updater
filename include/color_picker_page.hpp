#pragma once

#include <borealis.hpp>

// Selector de color RGB visual para Joy-Cons y Pro Controllers.
// Permite ajustar cada parte (cuerpo/botones) con sliders R/G/B
// y aplicar el resultado en vivo al mando.
class ColorPickerPage : public brls::View
{
public:
    enum class Controller
    {
        JoyCon,        // 4 partes: izq cuerpo/botones, der cuerpo/botones
        ProController  // 2 partes: cuerpo, botones
    };

    explicit ColorPickerPage(Controller type);

    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
    brls::View* getDefaultFocus() override;

private:
    Controller type;
    int numSlots;  // 4 para Joy-Con, 2 para Pro Controller

    // Cada parte guarda sus canales [R, G, B] (0-255)
    int slots[4][3];
    int currentSlot    = 0;
    int currentChannel = 0;  // 0=R, 1=G, 2=B

    void cycleSlot(int dir);
    void cycleChannel(int dir);
    void adjust(int delta);
    void apply();

    void drawJoyConPreview(NVGcontext* vg, int x, int y, unsigned width, int previewH, brls::FrameContext* ctx);
    void drawProControllerPreview(NVGcontext* vg, int x, int y, unsigned width, int previewH, brls::FrameContext* ctx);

    // Convierte una parte [R,G,B] al entero que espera el hardware (0xBBGGRR)
    static int toHardware(const int rgb[3]);
};
