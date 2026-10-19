#pragma once

#include <borealis.hpp>
#include <functional>

#include "cliente.hpp"

// Cliente OQB: sin certificado muestra el codigo de la consola para que OQB la
// registre y el boton de activar; con certificado, el codigo de soporte (QR).
class ClientePage : public brls::AppletFrame
{
private:
    brls::List* list;
    std::function<void(const cliente::Estado&)> alActivar;

    void construirRegistro(const cliente::Estado& estado);
    void construirSoporte(const cliente::Estado& estado);

public:
    // alActivar: para que quien abrio la pagina actualice lo que muestra.
    ClientePage(std::function<void(const cliente::Estado&)> alActivar = {});
};
