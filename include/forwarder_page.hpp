#pragma once

#include <switch.h>

#include <borealis.hpp>
#include <string>
#include <vector>

// Elegir cualquier NRO que ya esté en la SD y crearle un acceso directo en el
// menú HOME, sin tener que volver a descargar la app.
class ForwarderCreatePage : public brls::AppletFrame
{
private:
    brls::List* list;
    void populate();

public:
    ForwarderCreatePage();
};

// Listar los accesos directos ya creados y permitir borrarlos.
class ForwarderManagePage : public brls::AppletFrame
{
private:
    brls::List* list;
    void populate();
    void removeForwarder(u64 tid, const std::string& name);

public:
    ForwarderManagePage();
};
