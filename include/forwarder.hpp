// Generación e instalación de forwarders (accesos directos en el menú HOME)
// para NROs guardados en la SD.
//
// Portado de sphaira (https://github.com/ITotalJustice/sphaira), GPLv3,
// archivo `source/owo.cpp`, que a su vez se basa en hacbrewpack (creación de
// romfs/nca) y yati (instalación vía ncm/ns).
//
// No hace falta prod.keys: la única clave necesaria (header_key) se deriva de
// la propia consola con splCrypto.
#pragma once

#include <switch.h>

#include <functional>
#include <string>
#include <vector>

namespace fwd {

// Se llama con (paso, total) mientras avanza la instalación. `paso` nunca
// llega a `total`: quien llama marca el final recién cuando install() retorna,
// así la UI no puede darse por terminada mientras el worker sigue corriendo.
using ProgressFn = std::function<void(int step, int total)>;

struct Config
{
    std::string nro_path;  // ruta del NRO en la SD, ej. "/switch/JKSV.nro"
    std::string args;      // argumentos extra, normalmente vacío
    std::string name;      // nombre que se ve en el menú HOME
    std::string author;
    NacpStruct nacp{};
    std::vector<u8> icon;  // JPEG, tal como lo guarda el NRO
};

// Rellena `out` leyendo el asset section del NRO (icono + NACP).
// Si el NRO no trae assets, cae al nombre del archivo y al icono de esta app.
Result configFromNro(const std::string& nro_path, Config& out);

// Sólo el nombre que declara el NRO, sin leer el icono. Para listar muchos
// NROs sin cargar cientos de KB de imágenes que no se van a usar.
// Cae al nombre del archivo si el NRO no trae metadatos.
std::string nameFromNro(const std::string& nro_path);

// Construye los NCAs (program/control/meta) e instala el registro del título.
// Reinstalar el mismo NRO sobrescribe el forwarder anterior.
Result install(Config& config, const ProgressFn& progress = {}, NcmStorageId storage_id = NcmStorageId_SdCard);

// Elimina el forwarder asociado a un NRO, si existe.
Result remove(const std::string& nro_path, const std::string& args = "");

// true si ya hay un forwarder instalado para ese NRO.
bool exists(const std::string& nro_path, const std::string& args = "");

// Mensaje legible para un Result devuelto por este módulo.
std::string resultToString(Result rc);

}  // namespace fwd
