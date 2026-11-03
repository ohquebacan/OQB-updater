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

public:
    MainFrame();

    const std::string& getLatestTag() const
    {
        return this->latestTag;
    }

    const std::string& getDonationUrl() const
    {
        return this->donationUrl;
    }
};
