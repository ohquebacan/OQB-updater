#pragma once

#include <borealis.hpp>
#include <string>

#include "constants.hpp"

class MainFrame : public brls::TabFrame
{
private:
    // RefreshTask *refreshTask;

    // El último release publicado, consultado una vez al construir la ventana.
    // Vacío si no se pudo consultar.
    std::string latestTag;

    // El enlace de donacion: el del catalogo (enlaces.donar), o el de siempre.
    std::string donationUrl = DONATION_URL;

    // La pestana Apps, si hay versiones nuevas: se le dibuja encima el aviso.
    brls::View* appsItem = nullptr;

    // La pestana Nextendo mientras nunca se abrio: lleva un punto que invita.
    brls::View* nextendoItem = nullptr;

public:
    MainFrame();

    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;

    const std::string& getLatestTag() const
    {
        return this->latestTag;
    }

    const std::string& getDonationUrl() const
    {
        return this->donationUrl;
    }
};
