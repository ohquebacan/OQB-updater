#pragma once

#include <borealis.hpp>
#include <json.hpp>
#include <functional>
#include <string>
#include <vector>

/* Seccion "Nexus Bacán": juegos en un disco USB. Sale solo si el catalogo trae
   la clave "nexus_bacan" en nx-links.json:

     "nexus_bacan": {
       "url": "https://github.com/ohquebacan/OQB-HATS-PACK/releases/download/.../OQB-NEXUS-BACAN.zip",
       "firmwares": ["23.0.1", "23.5.0"],
       "presentacion": "...",          (opcional; si no, el texto de la app)
       "pasos": [ {"titulo": "...", "texto": "..."} ]   (opcional)
     }

   El pack de Nexus no es un pack completo: es lo poco que cambia sobre el pack
   normal (package3, stratosphere.romfs, fusee.bin, la app y el overlay). Se
   instala encima, con respaldo de lo que pisa, y se quita restaurando ese
   respaldo. No pasa por la instalacion de packs (ams_cfw) porque esa limpia
   /atmosphere entera. */
class NexusTab : public brls::List
{
public:
    /* `reinstalarPack`: abre la instalacion del pack con esa clave (la de
       Packs y firmwares). Se usa para volver al pack normal si no quedo
       respaldo. Puede venir vacia. */
    NexusTab(const nlohmann::ordered_json& nxlinks, const nlohmann::ordered_json& packVersions,
             std::function<bool(const std::string&)> reinstalarPack);

    static bool available(const nlohmann::ordered_json& nxlinks);

    /* Prueba sin publicar: si existe /config/aio-switch-updater/nexus_bacan_prueba.json,
       reemplaza la entrada "nexus_bacan" del catalogo. Puede traer "zip_local"
       (ruta en la SD) para instalar sin descargar. */
    static void aplicarPrueba(nlohmann::ordered_json& nxlinks);

    // Instalado y activo: el registro de la app y el USB encendido en la configuracion.
    static bool instalado();

    /* El diagnostico para soporte (USB, SD, modulos, arranque y errores): arma
       el archivo en /oqb/soporte/ y devuelve su ruta, vacia si no se pudo.
       Lo usan tambien los informes de error (Inicio › Estado). */
    static std::string diagnosticoSoporte();
    // Lo que lleva ese archivo, para decirlo antes de enviarlo.
    static std::vector<std::string> diagnosticoIncluye();
    // El subtitulo de la pagina: la version instalada (y si es la de overclock), o que es.
    static std::string subtitulo();

    // Hay que mirar la seccion: version nueva, o el pack normal lo quito al actualizarse.
    static bool necesitaAtencion(const nlohmann::ordered_json& nxlinks, const nlohmann::ordered_json& packVersions);

private:
    nlohmann::ordered_json nxlinks;
};
