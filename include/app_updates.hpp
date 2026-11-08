#pragma once

#include <cstdint>
#include <json.hpp>
#include <string>
#include <vector>

// Versiones nuevas de las apps del catalogo.
//
// Igual que con los packs: el enlace /releases/latest/download/ no cambia cuando
// sale una version, pero la fecha de subida del archivo si. El catalogo publica
// app-versions.json con esa fecha (URL -> {"fecha", "version"}); al instalar se
// anota en la SD la que tenia el archivo, y si el catalogo trae otra, hay una
// version nueva. Ademas, para las apps con .nro, el catalogo trae la version
// interna (NACP) de cada uno, y se compara con la del .nro que hay en la SD,
// como hace el gestor de homebrew de HomeNX: asi avisa aunque la app se haya
// instalado por otro lado. La fecha queda para lo que no tiene .nro.
namespace appUpdates {

    // Clave bajo la que viaja el json publicado dentro de nxlinks, para que
    // llegue a las listas sin cambiar como se construyen.
    constexpr const char NXLINKS_KEY[] = "_app_versions";

    // Descarga el json publicado. Objeto vacio si no se pudo: sin conexion no
    // se avisa de nada. Lo ultimo descargado queda en memoria para reutilizarlo
    // sin tocar la red (deep_clean lo usa al instalar el pack).
    nlohmann::ordered_json fetchPublished();

    // El ultimo catalogo descargado (objeto vacio si no se ha consultado o no
    // hubo conexion). Referencia a almacenamiento estatico: no copia el JSON.
    const nlohmann::ordered_json& catalogoGuardado();

    // La fecha publicada de esa URL, o vacia.
    std::string published(const nlohmann::ordered_json& nxlinks, const std::string& url);

    // Instalada desde una de nuestras apps y con otra fecha en el catalogo.
    bool hasUpdate(const nlohmann::ordered_json& nxlinks, const nlohmann::ordered_json& installed, const std::string& url);

    // Si la app esta en la SD: su .nro, el de un zip ("nros") o un archivo
    // propio de un zip sin .nro ("marcas"). Sin datos del catalogo, solo .nro.
    bool isInstalled(const nlohmann::ordered_json& nxlinks, const std::string& url);

    // La version interna (NACP) del .nro instalado de esa app, o vacia.
    std::string installedVersion(const std::string& url);
    // La version interna publicada en el catalogo ("nro_version"), o vacia.
    std::string publishedVersion(const nlohmann::ordered_json& nxlinks, const std::string& url);

    // El registro de la SD, para leerlo una vez por lista.
    nlohmann::ordered_json installed();

    // Se llama al terminar de instalar, con la fecha que estaba publicada.
    void recordInstalled(const std::string& url, const std::string& date);

    // Sube cada vez que termina una instalacion. Lo que muestra "version nueva"
    // (el punto del menu, la ficha de Bacán Shop, las filas de las listas) se
    // calcula al armar la ventana: al ver que cambio, se vuelve a calcular, y
    // asi una app recien actualizada deja de figurar como pendiente sin tener
    // que cerrar OQB Updater.
    int generacionInstalaciones();

    // Iconos. El catalogo nombra el de cada app ("icono") y los publica todos en
    // un zip; si falta alguno en la SD, se baja el zip (una sola peticion) y se
    // guardan en ICONOS_PATH. En segundo plano: las listas se arman sin esperar.
    void asegurarIconos(const nlohmann::ordered_json& catalogo);
    // La ruta del icono de esa app en la SD, o vacia si no hay.
    std::string rutaIcono(const nlohmann::ordered_json& nxlinks, const std::string& url);
    // Sin icono del catalogo: el del .nro que ya esta instalado (vacio si no).
    std::vector<uint8_t> iconoInstalado(const nlohmann::ordered_json& nxlinks, const std::string& url);

}  // namespace appUpdates
