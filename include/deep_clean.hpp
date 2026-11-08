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

// Despues de extraer el pack: quita de bootloader/ini los .ini de otros packs
// en los que ninguna entrada puede arrancar (sus payload=/fss0= no existen).
void quitarInisHuerfanos();

// Escribe una linea en deep_clean.log (append), para seguir la instalacion
// paso a paso desde la SD.
void logPaso(const std::string& paso);

// Vacia el registro. Cada tarea de WorkerPage lo llama al empezar, asi la
// ventana de log nunca muestra restos de una tarea anterior distinta.
void truncarLog();
}  // namespace deep_clean
