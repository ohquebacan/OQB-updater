#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Comprobacion de que las protecciones del pack siguen puestas.
//
// Rotar el package3 (o que una app de terceros toque system_settings.ini)
// puede dejar la consola sin bloqueo DNS sin avisar de nada. Esto lo verifica
// en la propia consola, que es donde importa.
namespace protection {

    enum class Status
    {
        Ok,       // como debe estar
        Warning,  // no es un fallo, pero conviene mirarlo
        Fail,     // la proteccion NO esta activa
    };

    struct Check
    {
        std::string name;  // que se comprobo
        Status status;
        std::string detail;  // por que salio asi
        /* Si esta comprobacion es parte del bloqueo DNS, que es lo que impide
           que la consola hable con Nintendo. Se marca para poder preguntar por
           ese bloqueo concreto sin tener que reconocer comprobaciones por su
           nombre, que es texto y cambia. */
        bool dnsRelated = false;
    };

    /* Lo que solo se sabe con la consola encendida: como resuelve el DNS de
       verdad y que versiones estan corriendo. Va aparte de los ficheros para
       que las pruebas puedan fingirlo igual que fingen la SD. */
    struct Runtime
    {
        enum class Dns
        {
            NotTested,   // no se probo (las pruebas, o fuera de la consola)
            Blocked,     // un host de Nintendo resolvio a 127.x o 0.0.0.0
            Redirected,  // resolvio a una direccion que sale a internet
            Unresolved,  // no resolvio: sin MITM y sin red no hay forma de saber
        };
        Dns dns = Dns::NotTested;
        std::string dnsDetail;   // el host y la direccion que dio
        uint32_t firmware = 0;   // (mayor << 16) | (menor << 8) | micro; 0 = no se sabe
        uint32_t atmosphere = 0; // igual que firmware
    };

    // Ejecuta todas las comprobaciones, en orden de importancia.
    //
    // `root` se antepone a las rutas. En la consola se deja vacio, que es la
    // raiz de la SD; las pruebas le pasan una carpeta temporal con una SD de
    // mentira, porque esta logica decide si avisamos de que la consola esta al
    // descubierto y no deberia verificarse solo leyendola.
    std::vector<Check> run(const std::string& root = "", const Runtime& rt = {});

#ifdef __SWITCH__
    // Prueba el DNS y lee las versiones en la consola.
    Runtime probeRuntime();

    // run() con lo que se mide en la consola. Es lo que usa la app.
    std::vector<Check> runOnConsole();
#endif

    // true si alguna comprobacion fallo.
    bool anyFailed(const std::vector<Check>& checks);

    /* true si el bloqueo DNS esta activo: el MITM encendido y un fichero de
       hosts que mande lo de Nintendo a ninguna parte.

       Lo pregunta quien va a hacer algo que solo es seguro con ese bloqueo
       puesto — activar la sincronizacion de hora del sistema hace que la
       consola intente contactar el NTP de Nintendo cada tanto, y lo unico que
       hace que esos intentos no salgan es esto. */
    bool dnsBlockingOk(const std::vector<Check>& checks);

}  // namespace protection
