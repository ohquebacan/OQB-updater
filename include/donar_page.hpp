#pragma once

#include <borealis.hpp>
#include <chrono>
#include <string>
#include <vector>

// Un QR dibujado con nanovg, cuadrado y centrado en el espacio que le den,
// con su margen blanco para que el telefono lo lea.
class QrCodeView : public brls::View
{
private:
    std::vector<uint8_t> qr;
    int size = 0;

public:
    explicit QrCodeView(const std::string& text);
    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
};

// Invitacion a donar: un texto, el QR del enlace de donacion y un boton "En
// otra ocasion" que se habilita tras una cuenta regresiva, para que alcance a
// leerse. Sale de vez en cuando al abrir la app (ver donar::showIfDue).
class DonarPage : public brls::View
{
private:
    brls::Label* title = nullptr;
    brls::Label* label = nullptr;
    brls::Button* button = nullptr;
    QrCodeView* qr = nullptr;
    std::chrono::system_clock::time_point start = std::chrono::high_resolution_clock::now();

public:
    explicit DonarPage(const std::string& url);
    ~DonarPage();

    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
    void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override;
    brls::View* getDefaultFocus() override;
};

namespace donar {

    // Cuenta una apertura de la app y, si toca, muestra la invitacion. Toca la
    // 5.a vez y despues cada 10: ni a quien recien la instala ni tan seguido que
    // moleste. `url` vacia = no se muestra.
    void showIfDue(const std::string& url);

}  // namespace donar
