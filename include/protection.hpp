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
    //
    // `root` se antepone a las rutas. En la consola se deja vacio, que es la
    // raiz de la SD; las pruebas le pasan una carpeta temporal con una SD de
    // mentira, porque esta logica decide si avisamos de que la consola esta al
    // descubierto y no deberia verificarse solo leyendola.
    std::vector<Check> run(const std::string& root = "");

    // true si alguna comprobacion fallo.
    bool anyFailed(const std::vector<Check>& checks);

}  // namespace protection
