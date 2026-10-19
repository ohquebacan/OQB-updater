#pragma once

#include <borealis.hpp>

// Resumen de los informes de error de atmosphere/crash_reports: que programa
// fallo, cuantas veces, cuando y la causa probable. Pensado para que el cliente
// entienda que paso sin abrir los .log, y para dar soporte con datos.
class DiagnosticsPage : public brls::AppletFrame
{
private:
    brls::List* list;

    void build();

public:
    DiagnosticsPage();
};
