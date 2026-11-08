#pragma once

#include <borealis.hpp>
#include <string>
#include <vector>

#include "protection.hpp"

// Primera pestaña: lo que conviene saber antes de tocar nada — qué versión
// corre, en qué consola, cuánto espacio queda y si las protecciones siguen
// puestas. Debajo queda la información de siempre sobre la app.
class AboutTab : public brls::List
{
public:
    // `tag` es el último release publicado, el mismo que ya consulta MainFrame.
    // Vacío significa que no se pudo consultar.
    // `donationUrl`: el enlace del QR para apoyar el proyecto; vacío = sin esa sección.
    // `checks`: las protecciones ya comprobadas por MainFrame (una consulta DNS
    // por ventana, no una por pestaña).
    AboutTab(const std::string& tag, const std::string& donationUrl, const std::vector<protection::Check>& checks);

private:
    void addStatusPanel(const std::string& tag, const std::vector<protection::Check>& checks);
    void addSupport(const std::string& donationUrl);
    void addAppInfo();
};
