#pragma once

#include <borealis.hpp>

// Resumen de los informes de error de atmosphere/crash_reports: que programa
// fallo, cuantas veces, cuando y la causa probable. Pensado para que el cliente
// entienda que paso sin abrir los .log, y para dar soporte con datos.
#include "crash_report.hpp"

// Nombre que reconoce el usuario para un programa de los informes. Lo usa
// tambien el codigo de soporte.
std::string crashProgramName(const crash::Group& g);

class DiagnosticsPage : public brls::AppletFrame
{
private:
    brls::List* list;

    void build();

public:
    DiagnosticsPage();
};
