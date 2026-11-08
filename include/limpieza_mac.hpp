#pragma once

#include <string>

// Basura que deja macOS al copiar a una SD FAT, con el mismo criterio que la app de PC:
// - "._nombre" solo si de verdad es AppleDouble (empieza con 00 05 16 07): la Switch los ve como
//   archivos reales y confunden a LayeredFS y a algunos homebrew;
// - ".DS_Store" en cualquier carpeta;
// - en la raiz, las carpetas de indexado de macOS (.Spotlight-V100, .fseventsd, .Trashes,
//   .TemporaryItems), que la consola no usa.
// No entra a /Nintendo ni a /emuMMC (datos de la consola) y no sigue enlaces.
namespace limpiezaMac {
    struct Resultado {
        int archivos = 0;
        int carpetas = 0;
        long long bytes = 0;
    };

    Resultado limpiar();

    // "Se quitaron N archivos y M carpetas (X MB)" o "no habia nada".
    std::string resumen(const Resultado& r);
}
