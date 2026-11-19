#pragma once

#include <switch.h>

#include <set>
#include <string>
#include <vector>

/* Íconos del HOME que se quedan cargando para siempre (0,0 MB): el registro
   del juego sigue en la consola, pero su contenido ya no está o está dañado,
   así que el sistema no puede leer ni el nombre ni el ícono.

   Cuenta como dañado solo si NO se puede leer la información del juego ni de
   la caché del sistema ni del contenido. Quedan fuera:
   - los juegos de cartucho (sin el cartucho puesto tampoco se leen);
   - los juegos del USB de Nexus que funcionan (tienen su ícono en la caché;
     los que Nexus dejó sin ícono ni contenido sí cuentan, y a esos solo se les
     quita el registro, nunca contenido);
   - los archivados desde el HOME (la caché tiene su ícono).
   Borrar quita el registro y lo que quede de su contenido; las partidas no se
   tocan. */
namespace homeRotos {
    struct Entrada {
        u64 id = 0;
        bool nexus = false;  // está en la tabla del USB de Nexus
    };

    std::vector<Entrada> buscar();

    // Cuántos se quitaron de verdad (se comprueba volviendo a listar).
    int quitar(const std::vector<Entrada>& lista);

    // "0100ABCD12345000" por línea, como mucho `maximo` y "... y N más".
    std::string listado(const std::vector<Entrada>& lista, size_t maximo = 6);

    // Juegos que Nexus tiene en el USB (ID con los últimos 13 bits en cero).
    std::set<u64> juegosNexus();

    // Quita solo el registro del juego (DeleteApplicationRecord), nunca contenido.
    Result quitarRegistro(u64 id);
}
