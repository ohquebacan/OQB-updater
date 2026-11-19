#pragma once

#include <switch.h>

#include <string>
#include <vector>

/* Limpiar la emuNAND (idea de «Clean emuMMC» de HATS-Tools, de sthetix).

   Una emuNAND recién hecha es una copia de la consola: trae los registros de
   todos los juegos y sus partidas, pero los juegos están en la carpeta
   Nintendo de la sysNAND, que la emuNAND no ve. En el HOME salen como íconos
   que nunca cargan o que piden descargar. Esto quita esos juegos (y, si se
   elige, las partidas) para dejarla como nueva.

   Solo funciona arrancado en emuNAND con su propia carpeta Nintendo
   (emuMMC/…/Nintendo): la ruta se le pide al monitor seguro, no a un .ini que
   pudo cambiar desde el arranque. Nunca toca la sysNAND.
   Los juegos del USB de Nexus no se tocan: sus archivos en el USB también los
   usa la sysNAND. Tampoco sus partidas. */
namespace limpiarEmunand {
    enum class Donde {
        Emunand,       // emuNAND con su carpeta Nintendo: se puede limpiar
        SysNand,       // arrancada en sysNAND
        SinCarpeta,    // emuNAND que usa la carpeta Nintendo de la SD
        NoSeSabe,      // no se pudo leer la configuración del arranque
    };

    struct Plan {
        Donde donde = Donde::NoSeSabe;
        std::string nintendo;                // carpeta Nintendo de la emuNAND
        std::vector<u64> juegos;             // se quitan
        std::vector<FsSaveDataInfo> partidas;  // se borran solo si se elige
        size_t actualizaciones = 0, dlc = 0;
        size_t nexus = 0;                    // juegos del USB de Nexus que se dejan
        size_t conArchivos = 0;              // juegos que sí tienen su contenido (esos abren)
        std::vector<std::string> identidad;  // para comprobar que nada cambió
        bool error = false;                  // falló una lectura: no se borra nada
    };

    Plan armar();

    struct Resultado {
        bool cambio = false;  // la emuNAND cambió entre la vista previa y borrar: no se borró nada
        int juegos = 0, juegosMal = 0;
        int partidas = 0, partidasMal = 0;
        int quedanJuegos = -1, quedanPartidas = -1;  // -1: no se pudo volver a revisar
    };

    Resultado ejecutar(const Plan& plan, bool borrarPartidas);
}
