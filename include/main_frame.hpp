#pragma once

#include <borealis.hpp>
#include <string>

class MainFrame : public brls::TabFrame
{
private:
    // RefreshTask *refreshTask;

    // El último release publicado, consultado una vez al construir la ventana.
    // Vacío si no se pudo consultar.
    std::string latestTag;

public:
    MainFrame();

    const std::string& getLatestTag() const
    {
        return this->latestTag;
    }
};
