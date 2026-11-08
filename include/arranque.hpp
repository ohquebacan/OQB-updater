#pragma once

#include <string>

// Tiempos del arranque, para saber qué fase hace esperar al abrir la app.
// Se escriben de una vez en /config/aio-switch-updater/arranque.log (se
// sobrescribe en cada apertura): en la SD no hay que abrir un archivo por paso.
namespace arranque {
    // Anota la fase con los segundos desde la primera marca. Seguro entre hilos.
    void marca(const std::string& fase);
    // Escribe lo anotado.
    void guardar();
}  // namespace arranque
