#pragma once

#include <borealis.hpp>

// Muestra el resultado de protection::run(): una linea por comprobacion, con
// su estado y el motivo.
class ProtectionPage : public brls::AppletFrame
{
private:
    brls::List* list;

public:
    ProtectionPage();
};
