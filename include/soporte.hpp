#pragma once

#include <functional>
#include <string>
#include <vector>

/* Enviar a soporte: manda un diagnostico al servidor de OQB Reportes
   (Cloudflare), que lo reenvia al grupo privado de Telegram. El token del bot
   vive solo en el servidor; aqui solo esta su direccion. */
namespace soporte {

    constexpr const char URL[] = "https://oqb-reportes.oqb-reportes.workers.dev/v1/report";
    // Lo minimo que tiene que contar el usuario (el servidor pide lo mismo).
    constexpr size_t MINIMO_MENSAJE = 15;
    // Entre dos envios desde la misma consola.
    constexpr long ESPERA_SEGUNDOS = 10 * 60;

    struct Resultado {
        bool ok = false;
        std::string id;     // R-XXXXXX
        std::string error;  // el "error" del servidor, o vacio si no hubo respuesta
        long http = 0;
    };

    // Manda el archivo de `ruta` (texto del diagnostico). `progreso` va de 0 a 1.
    Resultado enviar(const std::string& ruta, const std::string& app, const std::string& mensaje,
                     const std::string& contacto, std::function<void(float)> progreso);

    // Cuantos segundos faltan para poder enviar otro (0 = ya se puede).
    long faltaParaEnviar();

    // El @ de Telegram que dejo la ultima vez (sin la @), para no pedirlo de nuevo.
    std::string contactoGuardado();
    void guardarContacto(const std::string& contacto);

    // Lo que dice la pantalla segun el resultado: causa probable, nunca certeza.
    std::string textoError(const Resultado& r);

}  // namespace soporte

// La pantalla: que se envia, que paso, el @ opcional, y el codigo al final.
// `generar` arma el archivo y devuelve su ruta (vacia si no se pudo).
void abrirEnviarSoporte(const std::string& app, std::function<std::string()> generar, std::vector<std::string> incluye);
