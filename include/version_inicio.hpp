#pragma once

#include <switch.h>

#include <string>
#include <vector>

/* La «versión de inicio» que la consola guarda para cada juego: la más alta con
   la que se abrió. No se borra al borrar el juego, así que al reinstalar solo
   el juego base la consola pide la actualización. Se cambia igual que en
   Goldleaf (cnt::UpdateApplicationVersion): avm PushLaunchVersion y
   UpgradeLaunchRequiredVersion, y ns PushLaunchVersion (comando 36, como en
   libnx-ext de XorTroll). */
namespace versionInicio {
    // Pone la versión de inicio del juego. Devuelve el primer error, o 0.
    Result poner(u64 id, u32 version);

    struct Pendiente {
        u64 id = 0;
        u32 pide = 0;       // versión que la consola exige
        u32 instalada = 0;  // versión más alta instalada (juego o actualización)
        std::string nombre;
    };

    // Juegos instalados que piden una versión más nueva que la que tienen.
    std::vector<Pendiente> buscar();

    // Les pone la versión instalada. Devuelve cuántos quedaron bien.
    int arreglar(const std::vector<Pendiente>& lista);

    // Una línea por juego, como mucho `maximo`.
    std::string listado(const std::vector<Pendiente>& lista, size_t maximo = 5);
}
