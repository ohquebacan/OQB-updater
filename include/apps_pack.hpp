#pragma once

#include <json.hpp>
#include <cstdint>
#include <string>
#include <vector>

/* Apps del pack (Apps › Del pack).

   El catálogo publica apps-pack.json: para cada pack (por el nombre de su zip)
   la lista de sus apps y sysmodules, y de cada una sus archivos con su CRC y su
   tamaño, y un zip chico solo con ella. Aquí se compara eso con la SD para dar
   el estado de cada app, y se instala una o varias sin tocar Atmosphère,
   hekate ni el arranque (eso no está en esta lista: es el pack completo).

   Modos de cada archivo:
     0  siempre: se escribe y cuenta para el estado;
     1  si falta: ajustes del usuario, solo si no existen;
     2  si la app es nueva: los boot2.flag, para no volver a prender un
        sysmodule que alguien apagó. */
namespace appsPack {

    enum Estado {
        SinInstalar = 0,
        AlDia = 1,
        Nueva = 2,       // el pack trae otra versión
        Incompleta = 3,  // le falta un archivo
    };

    struct Archivo {
        std::string ruta;  // relativa a la raíz de la SD
        std::uint32_t crc = 0;
        std::uint64_t bytes = 0;
        int modo = 0;
        std::string version;  // de .nro y .ovl
    };

    struct App {
        std::string id, nombre, tipo, version, principal, zip, catalogo;
        std::uint64_t bytes = 0;  // lo que pesa su zip
        bool reinicio = false;    // sysmodule: hace falta reiniciar para que tome
        std::vector<Archivo> archivos;
        Estado estado = SinInstalar;
        std::string tengo;  // versión de la que hay en la SD
    };

    // apps-pack.json del catálogo, o vacío si no se pudo (sin conexión no hay sección).
    // Si existe /config/aio-switch-updater/apps_pack_local.json se usa ese (pruebas).
    nlohmann::ordered_json bajarManifiesto();

    // Las apps del pack que hay en la SD, con su estado. Vacía si no se sabe qué
    // pack es o el manifiesto no lo trae.
    std::vector<App> delPackActual(const nlohmann::ordered_json& manifiesto);

    // Vuelve a mirar la SD para una app.
    void releerEstado(App& app);

    struct Resultado {
        int bien = 0, mal = 0;
        bool reiniciar = false;
        std::vector<std::string> fallaron;
    };

    // Baja e instala cada una (desde un hilo de trabajo). Mueve la barra de progreso.
    Resultado instalar(const nlohmann::ordered_json& manifiesto, const std::vector<App>& apps);
}  // namespace appsPack
