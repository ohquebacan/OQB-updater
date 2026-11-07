#pragma once

#include <string>

namespace deep_clean {
// Restos de otros packs: sincroniza /atmosphere (fuera de contents) y /config
// con lo que trae el zip y conoce el catálogo, quita de /switch los .nro/.ovl
// que no son del pack ni del catálogo y elimina las carpetas de otros CFW de
// la raíz. Espejo de la limpieza profunda de OQB Updater PC: mismas reglas y
// mismas protecciones (carpetas con datos propios, Bacán Shop, sphaira,
// preserve.txt). La limpieza de rutina (/SaltySD, /atmosphere/contents,
// /bootloader) corre antes, en utils::extractArchive.
void removeForeignPackLeftovers(const std::string& archivePath);

// Escribe una linea en deep_clean.log (append), para seguir la instalacion
// paso a paso desde la SD.
void logPaso(const std::string& paso);
}  // namespace deep_clean
