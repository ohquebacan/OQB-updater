#pragma once

#include <cctype>
#include <cstdint>
#include <string>

/* Qué carpetas de atmosphere/contents sobreviven a la limpieza previa a instalar
   un HATS pack, además de preserve.txt y AMS_CONTENTS_KEEP.

   Cada carpeta se llama con el ID del programa que modifica. Las de juegos
   (IDs de aplicación, desde 0100000000010000) llevan lo del usuario:
   traducciones, mods de romfs, trucos, parches del exefs del juego. Nunca
   tienen un exefs.nsp suelto: eso solo lo tienen los sysmodules, que son lo que
   trae el pack y lo que hay que renovar para que no queden versiones viejas.

   Se conserva una carpeta si su nombre es un ID válido, no es del rango del
   sistema (0100000000000000-010000000000FFFF) y no tiene exefs.nsp. Ante la
   duda (nombre raro) se limpia, como antes. */
namespace contentsKeep {

    inline bool parsearId(const std::string& nombre, uint64_t& id)
    {
        if (nombre.size() != 16) return false;
        id = 0;
        for (char c : nombre) {
            const int d = std::isdigit((unsigned char)c) ? c - '0'
                        : (c >= 'a' && c <= 'f')        ? c - 'a' + 10
                        : (c >= 'A' && c <= 'F')        ? c - 'A' + 10
                                                        : -1;
            if (d < 0) return false;
            id = (id << 4) | (uint64_t)d;
        }
        return true;
    }

    inline bool esDelSistema(uint64_t id)
    {
        return id >= 0x0100000000000000ULL && id < 0x0100000000010000ULL;
    }

    // tieneExefsNsp: si la carpeta trae un exefs.nsp suelto (un sysmodule).
    inline bool esContenidoDeJuego(const std::string& nombre, bool tieneExefsNsp)
    {
        uint64_t id = 0;
        if (!parsearId(nombre, id)) return false;
        if (esDelSistema(id)) return false;
        return !tieneExefsNsp;
    }

}  // namespace contentsKeep
