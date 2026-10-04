#pragma once

#include <borealis.hpp>
#include <string>

// Primera pestaña: lo que conviene saber antes de tocar nada — qué versión
// corre, en qué consola, cuánto espacio queda y si las protecciones siguen
// puestas. Debajo queda la información de siempre sobre la app.
class AboutTab : public brls::List
{
public:
    // `tag` es el último release publicado, el mismo que ya consulta MainFrame.
    // Vacío significa que no se pudo consultar.
    explicit AboutTab(const std::string& tag = "");

private:
    void addStatusPanel(const std::string& tag);
    void addAppInfo();
};
