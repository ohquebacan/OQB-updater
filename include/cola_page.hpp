#pragma once

#include <borealis.hpp>

/* La cola de descargas (5.0.0): lo que se instala ahora con su barra, lo que
   espera (se puede quitar o cambiar de orden) y lo ultimo terminado. */
class ColaPage : public brls::View
{
public:
    ColaPage();
    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
    void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override {}
    brls::View* getDefaultFocus() override { return this; }

    // Abre la cola en una ventana.
    static void abrir();

private:
    int cursor = 0;  // 0: lo actual; 1..n: la espera
    void mover(int d);
};
