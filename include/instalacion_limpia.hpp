#pragma once

#include <json.hpp>

// Herramientas > Instalacion limpia del pack.
//
// Elige el pack, lo baja, recorre la SD sin borrar nada y muestra una vista
// previa: lo que siempre se conserva, lo que decide el usuario (apps que no son
// del pack, mods, temas, otras cosas de la raiz) y lo que siempre se borra
// (restos de otros packs y descargas viejas). Se confirma manteniendo A tres
// segundos; recien ahi se borra (anotando todo en limpieza.log) y se instala el
// pack por el camino de siempre. Cancelar en la vista previa no toca la SD.
namespace instalacionLimpia {
    void abrir(const nlohmann::ordered_json& nxlinks, bool erista);

    // Desde la tarjeta de un pack (Packs y firmwares): sin elegir el pack, y
    // primero una pantalla que explica exactamente que hace.
    void abrirCon(const nlohmann::ordered_json& nxlinks, bool erista, const std::string& nombre, const std::string& url);
}
