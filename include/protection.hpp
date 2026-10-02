#pragma once

#include <string>
#include <vector>

// Comprobacion de que las protecciones del pack siguen puestas.
//
// Rotar el package3 (o que una app de terceros toque system_settings.ini)
// puede dejar la consola sin bloqueo DNS sin avisar de nada. Esto lo verifica
// en la propia consola, que es donde importa.
namespace protection {

    enum class Status {
        Ok,       // como debe estar
        Warning,  // no es un fallo, pero conviene mirarlo
        Fail,     // la proteccion NO esta activa
    };

    struct Check {
        std::string name;    // que se comprobo
        Status status;
        std::string detail;  // por que salio asi
    };

    // Ejecuta todas las comprobaciones, en orden de importancia.
    std::vector<Check> run();

    // true si alguna comprobacion fallo.
    bool anyFailed(const std::vector<Check>& checks);

}  // namespace protection
