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
    // Sin el marco de foco alrededor de toda la ventana.
    void onFocusGained() override { this->focused = true; }
    void onFocusLost() override { this->focused = false; }

    // Abre la cola en una ventana.
    static void abrir();

private:
    int cursor = 0;  // 0: lo actual; 1..n: la espera
    void mover(int d);
};

/* La pantalla de siempre al instalar un port, pero sobre la cola (5.0.0):
   muestra el avance de ese trabajo y con B se sigue usando la app mientras
   baja (queda en Descargas, con −). */
class InstalacionPage : public brls::View
{
public:
    InstalacionPage(int id, const std::string& nombre);
    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
    void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override {}
    brls::View* getDefaultFocus() override { return this; }
    void onFocusGained() override { this->focused = true; }
    void onFocusLost() override { this->focused = false; }

    // Agrega el trabajo a la cola y abre su pantalla.
    static void abrir(int id, const std::string& nombre);

private:
    int id;
    std::string nombre;
    int fase = 0;  // 0 esperando, 1 instalando, 2 terminado
    bool listoAvisado = false;
};
