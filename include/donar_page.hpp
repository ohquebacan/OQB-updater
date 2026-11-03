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
    bool focusable = false;

public:
    explicit QrCodeView(const std::string& text);

    /* En una lista, borealis solo baja hasta la ultima vista que puede tener
       el foco: lo que hay debajo de ella nunca entra en pantalla. Con foco, el
       QR es alcanzable y la lista se desplaza hasta mostrarlo entero. */
    void setFocusable(bool focusable)
    {
        this->focusable = focusable;
    }

    brls::View* getDefaultFocus() override
    {
        return this->focusable ? this : nullptr;
    }
    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
};

// Una fila con el QR a la izquierda y lo que hay que hacer con el a la
// derecha. Juntos, para que el QR nunca aparezca solo en pantalla.
class QrFilaView : public brls::View
{
private:
    QrCodeView* qr;
    std::string texto;

public:
    QrFilaView(const std::string& url, const std::string& texto);
    ~QrFilaView();

    void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override;
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
    // El panel donde va todo, sobre la app atenuada.
    float panelX = 0, panelY = 0, panelW = 0, panelH = 0;

public:
    explicit DonarPage(const std::string& url);
    ~DonarPage();

    /* Translucida, como un dialogo de borealis: la app sigue debajo, atenuada,
       y al cerrar se desvanece todo junto. Opaca, borealis la quitaba primero
       y despues volvia a mostrar la app, y se veia un fondo negro en medio. */
    bool isTranslucent() override
    {
        return true;
    }
    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
    void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override;
    brls::View* getDefaultFocus() override;
};

// Una pagina de un flujo por etapas con un QR y su explicacion; "Continuar"
// pasa a la siguiente. La usa el firmware para el tutorial de Nextendo.
class QrStagePage : public brls::View
{
private:
    brls::StagedAppletFrame* frame;
    brls::Label* title = nullptr;
    brls::Label* label = nullptr;
    brls::Button* button = nullptr;
    QrCodeView* qr = nullptr;

public:
    QrStagePage(brls::StagedAppletFrame* frame, const std::string& title, const std::string& text, const std::string& url);
    ~QrStagePage();

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
