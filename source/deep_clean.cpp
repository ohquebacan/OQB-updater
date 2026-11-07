#include "deep_clean.hpp"

#include <switch.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include <minizip/unzip.h>

#include "constants.hpp"
#include "app_updates.hpp"
#include "extract.hpp"
#include "fs.hpp"

namespace {

std::string minusculas(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Prefijo de ruta, no de texto, y sin distinguir mayúsculas (la SD es FAT):
// "/a/0100B0" no es prefijo de "/a/0100B0FF". Igual que en OQB-updater PC.
bool prefijoDeRuta(const std::string& prefijo, const std::string& ruta)
{
    if (prefijo.empty() || prefijo.size() > ruta.size()) return false;
    for (size_t i = 0; i < prefijo.size(); ++i) {
        if (std::tolower((unsigned char)prefijo[i]) != std::tolower((unsigned char)ruta[i])) return false;
    }
    return prefijo.size() == ruta.size() || ruta[prefijo.size()] == '/';
}

// Rutas propias del ecosistema OQB: se conservan aunque no vengan en el pack
// ni en el catálogo (registros, tienda, menú sphaira).
constexpr const char* SIEMPRE[] = {"/config/aio-switch-updater", "/config/sphaira", "/switch/Bacan-Shop", "/switch/oqb-updater.nro"};
// Carpetas de otros CFW/packs que a veces quedan en la raíz de la SD.
constexpr const char* RAIZ_AJENA[] = {"ReiNX", "sxos", "sept"};

std::set<std::string> rutasDelZip(const std::string& archivo)
{
    std::set<std::string> out;
    const std::string envoltorio = extract::detectWrapperDir(archivo);
    unzFile zip = unzOpen(archivo.c_str());
    if (!zip) return out;
    if (unzGoToFirstFile(zip) == UNZ_OK) {
        do {
            unz_file_info info;
            char nombre[512];
            if (unzGetCurrentFileInfo(zip, &info, nombre, sizeof(nombre), nullptr, 0, nullptr, 0) != UNZ_OK) continue;
            std::string n = nombre;
            if (n.empty() || n.back() == '/' || n.rfind("__MACOSX/", 0) == 0) continue;
            const auto barra = n.find_last_of('/');
            const std::string base = barra == std::string::npos ? n : n.substr(barra + 1);
            if (base.rfind("._", 0) == 0 || base == ".DS_Store") continue;
            if (!envoltorio.empty() && n.rfind(envoltorio, 0) == 0) n = n.substr(envoltorio.size());
            if (n.empty()) continue;
            out.insert(minusculas("/" + n));
        } while (unzGoToNextFile(zip) == UNZ_OK);
    }
    unzClose(zip);
    return out;
}

struct Conocidos {
    std::set<std::string> rutas;   // rutas completas en minúsculas (nros/marcas del catálogo)
    std::set<std::string> nombres;  // nombres de .nro/.ovl en minúsculas (una app reconocida vale donde esté)
    bool delCatalogo = false;  // false = catálogo no disponible: no se sabe qué .nro son ajenos
};

// Lo que registra el catálogo (app-versions.json): las apps que se pueden
// instalar desde el updater y, con rutas exactas, las que vienen en zip.
// Se usa la copia que el arranque ya descargó: pedirla de nuevo acá era una
// red bloqueante en medio de la instalación (y choca con el curl global de
// otras descargas). Sin catálogo no se barre /switch: mejor no tocar nada
// que borrar apps del usuario por no saber cuáles son ajenas.
Conocidos conocidosDelCatalogo()
{
    Conocidos c;
    const nlohmann::ordered_json& versiones = appUpdates::catalogoGuardado();
    if (!versiones.is_object()) return c;
    c.delCatalogo = true;
    for (const auto& [url, detalle] : versiones.items()) {
        if (!detalle.is_object()) continue;
        const auto barra = url.find_last_of('/');
        const std::string baja = minusculas(barra == std::string::npos ? url : url.substr(barra + 1));
        if (baja.size() > 4 && (baja.ends_with(".nro") || baja.ends_with(".ovl"))) c.nombres.insert(baja);
        auto agregaRuta = [&c](const nlohmann::ordered_json& item, bool conClave) {
            std::string ruta;
            if (conClave) {
                if (item.contains("ruta") && item["ruta"].is_string()) ruta = item["ruta"].get<std::string>();
            } else if (item.is_string()) {
                ruta = item.get<std::string>();
            }
            if (ruta.empty()) return;
            if (ruta[0] != '/') ruta = "/" + ruta;
            ruta = minusculas(ruta);
            if (ruta == "/") return;
            c.nombres.insert(ruta.substr(ruta.find_last_of('/') + 1));
            c.rutas.insert(ruta);
        };
        if (detalle.contains("nros") && detalle["nros"].is_array()) {
            for (const auto& n : detalle["nros"]) agregaRuta(n, true);
        }
        if (detalle.contains("marcas") && detalle["marcas"].is_array()) {
            for (const auto& m : detalle["marcas"]) agregaRuta(m, false);
        }
    }
    return c;
}

// ¿Esta ruta (ya normalizada y en minúsculas) se conserva en la limpieza?
bool esConocida(const std::string& baja, const std::set<std::string>& blanca, const Conocidos& conocidos, const std::set<std::string>& conservar)
{
    if (blanca.count(baja) || conocidos.rutas.count(baja)) return true;
    for (const char* s : SIEMPRE) {
        if (prefijoDeRuta(s, baja)) return true;
    }
    for (const auto& crudo : conservar) {
        const std::string c = crudo.empty() || crudo[0] == '/' ? minusculas(crudo) : minusculas("/" + crudo);
        if (prefijoDeRuta(c, baja) || prefijoDeRuta(baja, c)) return true;
    }
    return false;
}

std::vector<std::string> nombresDe(const std::string& carpeta)
{
    std::vector<std::string> out;
    DIR* dir = opendir(carpeta.c_str());
    if (!dir) return out;
    while (dirent* ent = readdir(dir)) {
        const std::string n = ent->d_name;
        if (n != "." && n != "..") out.push_back(n);
    }
    closedir(dir);
    return out;
}

// Sincroniza una carpeta de la SD con lo reconocido: lo que no es del pack,
// del catálogo ni está protegido se borra en el acto. Devuelve true si la
// carpeta (o algo de ella) se conserva.
bool sincronizar(const std::string& carpeta, const std::set<std::string>& blanca, const Conocidos& conocidos, const std::set<std::string>& conservar)
{
    const auto nombres = nombresDe(carpeta);
    if (nombres.empty() && errno == ENOENT) return esConocida(minusculas(carpeta), blanca, conocidos, conservar);
    bool conserva = esConocida(minusculas(carpeta), blanca, conocidos, conservar);
    std::error_code ec;
    for (const auto& nombre : nombres) {
        if (nombre.rfind("._", 0) == 0) continue;
        const std::string ruta = carpeta + "/" + nombre;
        const std::string baja = minusculas(ruta);
        // /atmosphere/contents ya pasó por la limpieza de rutina (con
        // preserve.txt). Ojo: AMS_CONTENTS lleva '/' final y la ruta armada
        // aquí no; comparando contra la constante tal cual nunca coincidía y
        // se recorría (y borraba) todo contents, que puede tener miles de
        // entradas — esa era la pantalla congelada en "extrayendo".
        if (baja == "/atmosphere/contents") {
            conserva = true;
            continue;
        }
        if (std::filesystem::is_directory(ruta, ec)) {
            if (sincronizar(ruta, blanca, conocidos, conservar)) {
                conserva = true;
            } else {
                std::filesystem::remove_all(ruta, ec);
            }
        } else if (esConocida(baja, blanca, conocidos, conservar)) {
            conserva = true;
        } else {
            std::filesystem::remove(ruta, ec);
        }
    }
    return conserva;
}

// Quita de /switch los .nro y .ovl que no reconocemos. Si el .nro vive en una
// carpeta con otros archivos (un port con sus datos, un bundle propio), se
// respeta entero: solo caen las apps sueltas o carpetas que solo tienen apps.
void quitarNrosAjenos(const std::string& carpeta, int profundidad, const Conocidos& conocidos, const std::set<std::string>& conservar)
{
    if (profundidad == 0) return;
    const auto nombres = nombresDe(carpeta);
    if (nombres.empty()) return;
    const bool conDatos = carpeta != "/switch" && carpeta != "/switch/.overlays" &&
        std::any_of(nombres.begin(), nombres.end(), [](const std::string& n) {
            const std::string b = minusculas(n);
            return b.rfind("._", 0) != 0 && !b.ends_with(".nro") && !b.ends_with(".ovl");
        });
    std::error_code ec;
    for (const auto& nombre : nombres) {
        if (nombre.rfind("._", 0) == 0) continue;
        const std::string ruta = carpeta + "/" + nombre;
        const std::string baja = minusculas(ruta);
        bool protegida = false;
        for (const char* s : SIEMPRE) {
            if (prefijoDeRuta(s, baja)) protegida = true;
        }
        if (!protegida) {
            for (const auto& crudo : conservar) {
                const std::string c = crudo.empty() || crudo[0] == '/' ? crudo : "/" + crudo;
                if (prefijoDeRuta(c, ruta)) protegida = true;
            }
        }
        if (protegida) continue;
        if (std::filesystem::is_directory(ruta, ec)) {
            quitarNrosAjenos(ruta, profundidad - 1, conocidos, conservar);
        } else if (!conDatos && (baja.ends_with(".nro") || baja.ends_with(".ovl")) && !conocidos.nombres.count(minusculas(nombre))) {
            std::filesystem::remove(ruta, ec);
        }
    }
}

// Quita las carpetas de /switch que quedaron vacías (o solo con "._" del
// Finder) tras quitar apps ajenas. Devuelve true si queda algo.
bool podarVacias(const std::string& carpeta, int profundidad)
{
    if (profundidad == 0) return true;
    const auto nombres = nombresDe(carpeta);
    if (nombres.empty()) return true;
    bool quedaAlgo = false;
    std::error_code ec;
    for (const auto& nombre : nombres) {
        const std::string ruta = carpeta + "/" + nombre;
        const std::string baja = minusculas(ruta);
        if (nombre.rfind("._", 0) == 0) {
            std::filesystem::remove(ruta, ec);
            continue;
        }
        bool protegida = false;
        for (const char* s : SIEMPRE) {
            if (prefijoDeRuta(s, baja)) protegida = true;
        }
        if (protegida) {
            quedaAlgo = true;
            continue;
        }
        if (std::filesystem::is_directory(ruta, ec)) {
            if (podarVacias(ruta, profundidad - 1)) {
                quedaAlgo = true;
            } else {
                std::filesystem::remove(ruta, ec);
            }
        } else {
            quedaAlgo = true;
        }
    }
    return quedaAlgo;
}

}  // namespace

void deep_clean::removeForeignPackLeftovers(const std::string& archivePath)
{
    // Registro de pasos en la SD: si algo vuelve a frenar la instalación,
    // este archivo dice exactamente dónde quedó (sobrevive: vive en una
    // carpeta protegida).
    std::ofstream log(std::string(CONFIG_PATH) + "deep_clean.log", std::ios::trunc);
    auto paso = [&log](const std::string& p) { log << p << '\n'; log.flush(); };

    paso("inicio");
    std::set<std::string> conservar;
    for (const auto& c : fs::readLineByLine(FILES_IGNORE)) {
        conservar.insert(c.empty() || c[0] == '/' ? c : "/" + c);
    }
    const Conocidos conocidos = conocidosDelCatalogo();
    paso(conocidos.delCatalogo ? "catalogo: ok" : "catalogo: vacio");
    const std::set<std::string> blanca = rutasDelZip(archivePath);
    paso("zip: " + std::to_string(blanca.size()) + " rutas");
    for (const char* base : {"/atmosphere", "/config"}) {
        sincronizar(base, blanca, conocidos, conservar);
    }
    paso("atmosphere y config sincronizados");
    if (conocidos.delCatalogo) {
        quitarNrosAjenos("/switch", 3, conocidos, conservar);
        podarVacias("/switch", 3);
        paso("switch barrido");
    } else {
        paso("switch sin barrer (sin catalogo)");
    }
    std::error_code ec;
    for (const char* nombre : RAIZ_AJENA) {
        std::filesystem::remove_all(std::string("/") + nombre, ec);
    }
    paso("fin");
}
