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
        /* Las medidas salen del texto, no de la vista: para una etiqueta de una
           sola linea ListItem::layout llama a setBoundaries(x, y, 0, 0), asi que
           getHeight() es cero y el halo quedaba como una banda fina pegada al
           borde de arriba en vez de cubrir el texto. getTextWidth/getTextHeight
           son los limites reales de los glifos. */
        const float anchoTexto = (float)this->valueView->getTextWidth();
        const float altoTexto  = (float)this->valueView->getTextHeight();

        // Antes del primer layout todavia no hay medidas; mejor no pintar nada.
        if (anchoTexto > 0.0f && altoTexto > 0.0f) {
            /* Un periodo de unos dos segundos y medio: mas rapido parpadea y
               molesta, mas lento no se nota que se mueve. */
            const float pulso = 0.5f + 0.5f * std::sin(fase() * 2.5f);
            const unsigned char alpha = (unsigned char)(38.0f + 54.0f * pulso);

            const float holguraX = 16.0f;
            const float holguraY = 9.0f;

            /* El texto va alineado a la derecha y arriba, y tras el layout la x
               de la etiqueta ya es su borde izquierdo. Centrar el halo es
               entonces restar la misma holgura por cada lado. */
            const float bx = this->valueView->getX() - holguraX;
            const float by = this->valueView->getY() - holguraY;
            const float bw = anchoTexto + holguraX * 2.0f;
            const float bh = altoTexto + holguraY * 2.0f;

            const NVGcolor base = colorAviso();

            nvgBeginPath(vg);
            nvgRoundedRect(vg, bx, by, bw, bh, bh / 2.0f);
            nvgFillColor(vg, this->a(nvgRGBA((unsigned char)(base.r * 255), (unsigned char)(base.g * 255), (unsigned char)(base.b * 255), alpha)));
            nvgFill(vg);
        }
    }

    brls::ListItem::draw(vg, x, y, width, height, style, ctx);
}
