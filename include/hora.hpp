#pragma once

#include <string>

/* La hora de la consola: sincronizarla con un servidor de hora y el interruptor
   de sincronizacion por internet del sistema. Antes en Herramientas; ahora en el
   checklist de Nextendo, que es donde una hora mal puesta rompe la conexion. */
namespace hora {

    // Consulta un servidor de hora y cuenta que paso.
    void sincronizar();

    // Lee el interruptor del sistema. false si no se pudo leer.
    bool leerAutomatica(bool& activa);

    // Activa o desactiva el interruptor, con sus confirmaciones.
    void alternarAutomatica();

}  // namespace hora
