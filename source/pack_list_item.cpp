#include "pack_list_item.hpp"

#include <chrono>
#include <cmath>

namespace {

    /* Ambar. Sobre el tema claro hay que bajarlo bastante o no se lee: el fondo
       es casi blanco y un ambar brillante desaparece. */
    NVGcolor colorAviso()
    {
        return brls::Application::getThemeVariant() == brls::ThemeVariant::DARK
                   ? nvgRGB(255, 196, 64)
                   : nvgRGB(176, 106, 0);
    }

    // Segundos desde que arranco la app. Compartido, asi varios items pulsan a la par.
    float fase()
    {
        static const auto inicio = std::chrono::steady_clock::now();
        const auto ahora = std::chrono::steady_clock::now();
        return std::chrono::duration<float>(ahora - inicio).count();
    }

}  // namespace

PackListItem::PackListItem(const std::string& label, const std::string& description, const std::string& subLabel)
    : brls::ListItem(label, description, subLabel)
{
}

void PackListItem::setWarningValue(const std::string& value)
{
    /* Sin animacion de texto: el item se crea ya con este valor, asi que animar
       la transicion desde una cadena vacia solo lo hace aparecer tarde. */
    this->setValue(value, false, false);
    this->warning = true;

    if (this->valueView)
        this->valueView->setColor(colorAviso());
}

void PackListItem::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    if (this->warning && this->valueView) {
        /* Halo detras del texto. Un periodo de unos dos segundos y medio: mas
           rapido parpadea y molesta, mas lento no se nota que se mueve. */
        const float pulso = 0.5f + 0.5f * std::sin(fase() * 2.5f);
        const unsigned char alpha = (unsigned char)(38.0f + 54.0f * pulso);

        const float margenX = 14.0f;
        const float margenY = 7.0f;
        const float bx = this->valueView->getX() - margenX;
        const float by = this->valueView->getY() - margenY;
        const float bw = this->valueView->getWidth() + margenX * 2.0f;
        const float bh = this->valueView->getHeight() + margenY * 2.0f;

        const NVGcolor base = colorAviso();

        nvgBeginPath(vg);
        nvgRoundedRect(vg, bx, by, bw, bh, bh / 2.0f);
        nvgFillColor(vg, this->a(nvgRGBA((unsigned char)(base.r * 255), (unsigned char)(base.g * 255), (unsigned char)(base.b * 255), alpha)));
        nvgFill(vg);
    }

    brls::ListItem::draw(vg, x, y, width, height, style, ctx);
}
