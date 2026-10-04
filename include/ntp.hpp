#pragma once

#include <string>

// Pone en hora el reloj de la consola contra un servidor NTP.
//
// En un pack con los servidores de Nintendo bloqueados, la sincronización
// automática del sistema no funciona — va contra ntp.nintendo.net, que está
// justo entre lo que se bloquea — así que el reloj se va quedando atrás solo.
// Esto hace lo mismo que QuickNTP, pero desde la propia app.
namespace ntp {

    struct SyncResult
    {
        bool ok = false;
        // Segundos que estaba desviado el reloj antes de corregirlo. Negativo
        // significa que la consola iba atrasada.
        long long drift = 0;
        // Si no se aplicó nada porque ya estaba en hora.
        bool alreadyInSync = false;
        // Servidor que respondió, o el motivo del fallo si ok es false.
        std::string detail;
    };

    // Prueba los servidores en orden y se queda con el primero que responda.
    // No toca la interfaz: puede llamarse desde otro hilo.
    SyncResult sync();

    // Arranca la sincronización en segundo plano, una vez por ejecución. No
    // bloquea: al abrir la app no se puede esperar a la red.
    void syncInBackground();

    // Espera a que termine la de segundo plano, si hay alguna. Se llama al
    // salir para no dejar un hilo vivo cuando el proceso se cierra.
    void waitForBackgroundSync();

}  // namespace ntp
