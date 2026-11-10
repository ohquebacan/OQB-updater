#pragma once

#include <borealis.hpp>

/* Dialog de borealis siempre centrado. Cuando el texto no entra en su alto fijo
   (style->Dialog.height), borealis agranda la ventana solo hacia abajo y queda
   pegada al borde. Aqui se le da el alto que haga falta ANTES de acomodarla, asi
   el texto entra y la ventana se calcula centrada. */
class DialogoCentrado : public brls::Dialog
{
public:
    DialogoCentrado(const std::string& texto, unsigned alto) : brls::Dialog(texto), alto(alto) {}

    void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override
    {
        const unsigned antes = style->Dialog.height;
        style->Dialog.height = this->alto;
        brls::Dialog::layout(vg, style, stash);
        style->Dialog.height = antes;
    }

private:
    unsigned alto;
};
