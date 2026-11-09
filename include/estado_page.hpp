#pragma once

#include <borealis.hpp>
#include <string>
#include <vector>

#include "cliente.hpp"
#include "protection.hpp"

class QrCodeView;

/* Estado (rediseno): las protecciones resumidas con cada comprobacion, el
   registro de Cliente OQB, el apoyo al proyecto y el descargo. Los datos de la
   consola (firmware, espacio...) ya estan en Inicio y aqui no se repiten. */
class EstadoView : public brls::View
{
public:
    EstadoView(const std::string& donationUrl, const std::vector<protection::Check>& checks);
    ~EstadoView() override;

    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
    void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override {}
    brls::View* getDefaultFocus() override { return this; }
    void onFocusGained() override { this->focused = true; }
    void onFocusLost() override { this->focused = false; }

private:
    enum Objetivo { Protecciones, Cliente, Apoyo, Diagnostico, Descargo };
    std::vector<Objetivo> objetivos;
    int elegido = 0;

    std::string donacion;
    std::vector<protection::Check> checks;
    cliente::Estado cliente;
    QrCodeView* qr = nullptr;
    // Resumen del diagnostico de errores (se lee al abrir la pantalla).
    int errores = 0, programasConError = 0;
    std::string ultimoNombre, ultimoCodigo, ultimaFecha;
    int fuente = -1;
    bool fuenteIntentada = false;

    void abrir();
    bool mover(int dx, int dy);
};

// El marco sin cabecera, como el menu principal: la pagina trae su titulo.
class EstadoFrame : public brls::AppletFrame
{
public:
    EstadoFrame(const std::string& donationUrl, const std::vector<protection::Check>& checks);
    void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override;
    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
};
