#pragma once

#include <switch.h>

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "constants.hpp"

namespace extract {
    static constexpr u32 MaxTitleCount = 64000;
    typedef struct Title
    {
        std::string id;
        std::string name;
        bool operator==(const Title& x) const
        {
            return id == x.id;
        }

        bool operator<(const Title& x) const
        {
            return id < x.id;
        }
    } Title;

    // Algunos zips de homebrew vienen envueltos en una carpeta unica (SwitchU usa
    // "sd_out/") cuyo contenido es lo que hay que volcar en la raiz. Devuelve esa
    // carpeta, o "" si el zip ya trae sus rutas tal cual. Nunca devuelve una
    // carpeta que de verdad exista en la raiz de la SD, para no destripar un zip
    // que legitimamente solo traiga, por ejemplo, switch/.
    std::string detectWrapperDir(const std::string& archivePath);

    // Si cabe en la SD lo que trae el zip descomprimido (con un 10 % de margen).
    // true si no se pudo saber cuanto espacio queda.
    bool hayEspacioPara(const std::string& archivePath);

    void extract(
        const std::string& filename, const std::string& workingPath = ROOT_PATH, bool preserveInis = false, std::function<void()> func = []() { return; },
        const std::string& stripPrefix = "");
    // Ruta que tendrá el .nro del archivo una vez extraído bajo workingPath, o
    // "" si el zip no trae ninguno. Se prefiere el que caiga en /switch/.
    std::string findNroInArchive(const std::string& archivePath, const std::string& workingPath = ROOT_PATH, const std::string& stripPrefix = "");
    std::vector<std::string> getInstalledTitlesNs(const std::function<void(int, int)>& avance = {});
    /** La lista de juegos instalados ya se conoce para los registros actuales (sin leer cada juego). */
    bool installedTitlesKnown();
    /** Guarda la lista que alguien ya armo leyendo cada juego, para no repetir la lectura. */
    void rememberInstalledTitles(const std::vector<std::string>& titles);
    std::vector<std::string> excludeTitles(const std::string& path, const std::vector<std::string>& listedTitles);
    void writeTitlesToFile(const std::set<std::string>& titles, const std::string& path);
    void extractCheats(const std::string& archivePath, const std::vector<std::string>& titles, CFW cfw, const std::string& version, bool extractAll = false, bool marcarFin = true);
    void extractAllCheats(const std::string& archivePath, CFW cfw, const std::string& version);
    void removeCheats();
    void removeOrphanedCheats();
    bool removeCheatsDirectory(const std::string& entry);
    bool isBID(const std::string& bid);
}  // namespace extract