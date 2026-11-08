#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

// Instalacion limpia (Herramientas): deja en la SD, en lo posible, solo lo que
// trae el pack. Primero se arma un plan sin borrar nada (para la vista previa) y
// solo despues de confirmar se ejecuta, anotando cada cosa en limpieza.log.
// Nunca entra al plan: /Nintendo, /emuMMC, /backup, /JKSV, los respaldos de
// Atmosphere, lo de preserve.txt, juegos sueltos (.nsp/.xci), las llaves (.keys)
// ni lo de Nextendo/Prelude.
namespace estricta {
    // PORTS: una app ajena con mas de 256 MB (un port con los datos del juego, un
    // emulador con sus juegos): es contenido del usuario, por omision se queda.
    enum Categoria { RESTOS = 0, APPS, MODS, TEMAS, RAIZ, PORTS, N_CATEGORIAS };

    struct Elemento {
        std::string ruta;
        uint64_t bytes = 0;
        Categoria cat = RESTOS;
    };

    struct Plan {
        bool valido = false;
        std::vector<Elemento> elementos;
        uint64_t bytes[N_CATEGORIAS] = {};
        int cuenta[N_CATEGORIAS] = {};
        uint64_t usado = 0;  // ocupado en la SD al armar el plan
    };

    // conservar[cat]: true = esa categoria se queda. RESTOS siempre se borra.
    struct Opciones {
        bool conservar[N_CATEGORIAS] = {false, false, true, true, false, true};
    };

    // Lee el zip del pack y recorre la SD. No borra nada.
    Plan armar(const std::string& zipPath);

    uint64_t aBorrar(const Plan& plan, const Opciones& op);

    // Borra lo del plan que no se conserva. Devuelve cuantas cosas quito.
    int ejecutar(const Plan& plan, const Opciones& op);
}  // namespace estricta
}  // namespace deep_clean
