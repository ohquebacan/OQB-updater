#pragma once

#include <json.hpp>
#include <string>

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
    // se avisa de nada.
    nlohmann::ordered_json fetchPublished();

    // La fecha publicada de esa URL, o vacia.
    std::string published(const nlohmann::ordered_json& nxlinks, const std::string& url);

    // Instalada desde una de nuestras apps y con otra fecha en el catalogo.
    bool hasUpdate(const nlohmann::ordered_json& nxlinks, const nlohmann::ordered_json& installed, const std::string& url);

    // El registro de la SD, para leerlo una vez por lista.
    nlohmann::ordered_json installed();

    // Se llama al terminar de instalar, con la fecha que estaba publicada.
    void recordInstalled(const std::string& url, const std::string& date);

}  // namespace appUpdates
