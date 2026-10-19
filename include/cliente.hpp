#pragma once

#include <json.hpp>
#include <string>

// Certificado de cliente OQB: lo emite la pagina privada de OQB
// (ohquebacan.github.io/oqb-clientes) firmandolo con su clave, y la app lo
// verifica con la clave publica. Va atado al Device ID de la consola (de los
// fusibles, no del PRODINFO), asi que copiarlo a otra consola no sirve.
//
// El formato es el de oqb-cliente.js en ese repositorio; si cambia alla,
// cambia aca.
namespace cliente {

    struct Estado
    {
        bool valido = false;
        std::string idhex;   // Device ID en 16 hex minusculas
        std::string nombre;
        std::string fecha;   // AAAA-MM-DD, la del registro
        std::string firma;   // base64, va en el codigo de soporte
    };

    // "XXXX-XXXX-XXXX-XXXX-CC": lo que se le dicta a OQB. CC detecta errores
    // al copiarlo.
    std::string codigoDe(const std::string& idhex);

    // Nombre del archivo del certificado en el repositorio publico.
    std::string archivoDe(const std::string& idhex);

    // Verifica un certificado para esa consola. Si vale, deja el nombre del
    // cliente (descifrado) en `nombre`.
    bool verificar(const std::string& idhex, const nlohmann::ordered_json& cert, std::string& nombre);

    // Lo que viaja en el QR de soporte, como enlace a la pagina de OQB que lo
    // verifica y lo muestra.
    std::string urlSoporte(const nlohmann::ordered_json& datos);

#ifdef __SWITCH__
    // Device ID de esta consola, o vacio si no se pudo leer.
    std::string idConsola();

    // Lee el certificado guardado en la SD y lo verifica.
    Estado cargar();

    enum class Activacion
    {
        Ok,
        NoRegistrada,  // todavia no hay certificado publicado para esta consola
        SinRed,
        Invalida,      // hay algo publicado pero no es valido para esta consola
    };

    // Busca el certificado publicado de esta consola, lo verifica y lo guarda.
    Activacion activar(Estado& out);
#endif

}  // namespace cliente
