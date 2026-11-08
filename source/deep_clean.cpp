#include "deep_clean.hpp"

#include <switch.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <functional>
#include <dirent.h>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include <minizip/unzip.h>

#include "constants.hpp"
#include "actividad.hpp"
#include "app_updates.hpp"
#include "extract.hpp"
#include "fs.hpp"
#include "limpieza_mac.hpp"
#include "utils.hpp"

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

// Criterio (el mismo de OQB Updater PC), de lo que más choca a lo que menos:
// - /atmosphere (fuera de contents) es lo que puede romper el arranque: queda
//   igual a lo que trae el pack, salvo lo que escriben Atmosphère y la consola.
// - /config: se conserva la carpeta de cada app que sigue en la SD, que trae el
//   pack o que registra el catálogo. La de una app que ya no está es basura.
// - /switch es del usuario: sus homebrew y ports se quedan aunque no los
//   conozcamos. Solo cae la segunda copia de una app que el pack ya trae (mismo
//   nombre dentro del .nro, otro archivo) y los .nro/.ovl vacíos o dañados. La
//   basura conocida de otros packs se quita con pack_remove.txt.
// - Las carpetas de otros CFW en la raíz (ReiNX, sxos, sept).

// Rutas propias del ecosistema OQB y lo que escriben Atmosphère y la consola
// (automatic_backups guarda el PRODINFO; los informes de error los lee el
// diagnóstico de esta app).
constexpr const char* SIEMPRE[] = {"/config/aio-switch-updater", "/config/sphaira", "/config/MissionControl", "/switch/Bacan-Shop",
                                   "/atmosphere/automatic_backups", "/atmosphere/crash_reports", "/atmosphere/fatal_reports",
                                   "/atmosphere/fatal_errors", "/atmosphere/erpt_reports", "/atmosphere/logs", "/atmosphere/flags",
                                   "/atmosphere/bluetooth_devices.db"};
// Carpetas de otros CFW/packs que a veces quedan en la raíz de la SD.
constexpr const char* RAIZ_AJENA[] = {"ReiNX", "sxos", "sept"};

/* Carpetas: con fsFsDeleteDirectoryRecursively (fs::removeDir), lo mismo que
   la limpieza de rutina y pack_remove.txt. std::filesystem::remove_all en la
   Switch deja archivos sin borrar y el error queda en el error_code sin avisar. */
void borrarCarpeta(const std::string& ruta)
{
    if (!fs::removeDir(ruta))
        deep_clean::logPaso("   no se pudo borrar dir " + ruta);
}

bool esApp(const std::string& baja)
{
    return baja.ends_with(".nro") || baja.ends_with(".ovl");
}

std::string archivoDe(const std::string& ruta)
{
    const auto barra = ruta.find_last_of('/');
    return barra == std::string::npos ? ruta : ruta.substr(barra + 1);
}

// Lector por desplazamiento: sirve igual para un archivo de la SD y para un
// .nro leído del zip a memoria.
using LeerEn = std::function<bool(u64 desde, void* destino, size_t largo)>;

// ¿Es un NRO (también los .ovl)? Lleva "NRO0" en 0x10.
bool esNro(const LeerEn& leer)
{
    char magia[4];
    return leer(0x10, magia, 4) && std::memcmp(magia, "NRO0", 4) == 0;
}

// El nombre de la app dentro del NACP (el primer idioma que lo tenga), en
// minúsculas; vacío si el NRO no trae metadatos.
std::string nombreApp(const LeerEn& leer)
{
    u32 tamNro = 0;
    char aset[0x38];
    if (!leer(0x18, &tamNro, 4) || !leer(tamNro, aset, sizeof(aset)) || std::memcmp(aset, "ASET", 4) != 0) return "";
    u64 inicio = 0, tam = 0;
    std::memcpy(&inicio, aset + 0x18, 8);
    std::memcpy(&tam, aset + 0x20, 8);
    if (tam < 0x3000) return "";
    std::vector<char> nacp(0x3000);
    if (!leer(tamNro + inicio, nacp.data(), nacp.size())) return "";
    for (size_t idioma = 0; idioma < 16; ++idioma) {
        const char* n = nacp.data() + idioma * 0x300;
        std::string nombre(n, strnlen(n, 0x200));
        while (!nombre.empty() && std::isspace((unsigned char)nombre.back())) nombre.pop_back();
        while (!nombre.empty() && std::isspace((unsigned char)nombre.front())) nombre.erase(0, 1);
        if (!nombre.empty()) return minusculas(nombre);
    }
    return "";
}

LeerEn lectorDeMemoria(const std::vector<char>& datos)
{
    return [&datos](u64 desde, void* destino, size_t largo) {
        if (desde + largo > datos.size()) return false;
        std::memcpy(destino, datos.data() + desde, largo);
        return true;
    };
}

LeerEn lectorDeArchivo(FILE* f)
{
    return [f](u64 desde, void* destino, size_t largo) {
        return std::fseek(f, (long)desde, SEEK_SET) == 0 && std::fread(destino, 1, largo, f) == largo;
    };
}

// Lo que trae el zip del pack.
struct DelZip {
    std::set<std::string> rutas;        // todos sus archivos, en minúsculas
    std::set<std::string> archivosApp;  // nombres de archivo de sus .nro/.ovl de /switch
    std::set<std::string> apps;         // nombres de app (NACP) de esos .nro/.ovl
};

DelZip leerZip(const std::string& archivo)
{
    DelZip d;
    const std::string envoltorio = extract::detectWrapperDir(archivo);
    unzFile zip = unzOpen(archivo.c_str());
    if (!zip) return d;
    if (unzGoToFirstFile(zip) == UNZ_OK) {
        do {
            unz_file_info info;
            char nombre[512];
            if (unzGetCurrentFileInfo(zip, &info, nombre, sizeof(nombre), nullptr, 0, nullptr, 0) != UNZ_OK) continue;
            std::string n = nombre;
            if (n.empty() || n.back() == '/' || n.rfind("__MACOSX/", 0) == 0) continue;
            const std::string base = archivoDe(n);
            if (base.rfind("._", 0) == 0 || base == ".DS_Store") continue;
            if (!envoltorio.empty() && n.rfind(envoltorio, 0) == 0) n = n.substr(envoltorio.size());
            if (n.empty()) continue;
            const std::string ruta = minusculas("/" + n);
            d.rutas.insert(ruta);
            // Un .nro pesa unos pocos MB; con tope por si el zip trae algo raro.
            if (ruta.rfind("/switch/", 0) == 0 && esApp(ruta)) {
                d.archivosApp.insert(archivoDe(ruta));
                if (info.uncompressed_size < (64u << 20) && unzOpenCurrentFile(zip) == UNZ_OK) {
                    std::vector<char> datos(info.uncompressed_size);
                    const int leidos = unzReadCurrentFile(zip, datos.data(), datos.size());
                    unzCloseCurrentFile(zip);
                    if (leidos == (int)datos.size()) {
                        const std::string app = nombreApp(lectorDeMemoria(datos));
                        if (!app.empty()) d.apps.insert(app);
                    }
                }
            }
        } while (unzGoToNextFile(zip) == UNZ_OK);
    }
    unzClose(zip);
    return d;
}

// Lo que registra el catálogo (app-versions.json). Solo suma protecciones: si
// no se pudo descargar, la limpieza no borra más por eso. Se usa la copia que
// el arranque ya descargó (pedirla acá era red bloqueante en plena instalación).
struct Conocidos {
    std::set<std::string> rutas;    // rutas completas en minúsculas (nros/marcas)
    std::set<std::string> nombres;  // nombres de .nro/.ovl en minúsculas
};

Conocidos conocidosDelCatalogo()
{
    Conocidos c;
    const nlohmann::ordered_json& versiones = appUpdates::catalogoGuardado();
    if (!versiones.is_object()) return c;
    for (const auto& [url, detalle] : versiones.items()) {
        if (!detalle.is_object()) continue;
        const std::string baja = minusculas(archivoDe(url));
        if (baja.size() > 4 && esApp(baja)) c.nombres.insert(baja);
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
            c.nombres.insert(archivoDe(ruta));
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

// Protegida por SIEMPRE o por preserve.txt (en las dos direcciones: lo que está
// dentro de algo conservado, y la carpeta que contiene algo conservado).
bool protegida(const std::string& baja, const std::set<std::string>& conservar)
{
    for (const char* s : SIEMPRE) {
        if (prefijoDeRuta(s, baja)) return true;
    }
    for (const auto& c : conservar) {
        if (prefijoDeRuta(c, baja) || prefijoDeRuta(baja, c)) return true;
    }
    return false;
}

bool conocida(const std::string& baja, const DelZip& zip, const Conocidos& conocidos, const std::set<std::string>& conservar)
{
    return zip.rutas.count(baja) || conocidos.rutas.count(baja) || protegida(baja, conservar);
}

std::vector<std::string> nombresDe(const std::string& carpeta, bool* existe = nullptr)
{
    std::vector<std::string> out;
    DIR* dir = opendir(carpeta.c_str());
    if (existe) *existe = dir != nullptr;
    if (!dir) return out;
    while (dirent* ent = readdir(dir)) {
        const std::string n = ent->d_name;
        if (n != "." && n != "..") out.push_back(n);
    }
    closedir(dir);
    return out;
}

bool esCarpeta(const std::string& ruta)
{
    std::error_code ec;
    return std::filesystem::is_directory(ruta, ec);
}

// Deja /atmosphere igual a lo que trae el pack: borra en el acto lo que no viene
// en el zip, no registra el catálogo ni está protegido. Devuelve true si la
// carpeta (o algo de ella) se conserva.
bool sincronizar(const std::string& carpeta, const DelZip& zip, const Conocidos& conocidos, const std::set<std::string>& conservar)
{
    bool existe = false;
    const auto nombres = nombresDe(carpeta, &existe);
    if (!existe) return true;
    bool conserva = conocida(minusculas(carpeta), zip, conocidos, conservar);
    std::error_code ec;
    for (const auto& nombre : nombres) {
        const std::string ruta = carpeta + "/" + nombre;
        if (nombre.rfind("._", 0) == 0) {
            std::filesystem::remove(ruta, ec);  // basura del Finder/Mac
            continue;
        }
        const std::string baja = minusculas(ruta);
        // /atmosphere/contents ya pasó por la limpieza de rutina (con
        // preserve.txt). AMS_CONTENTS lleva '/' final y esta ruta no: por eso
        // se compara con el texto, no con la constante.
        if (baja == "/atmosphere/contents") {
            conserva = true;
            continue;
        }
        if (esCarpeta(ruta)) {
            if (sincronizar(ruta, zip, conocidos, conservar)) {
                conserva = true;
            } else {
                deep_clean::logPaso("   borro dir " + ruta);
                actividad::detalle(actividad::texto("quitado", ruta));
                borrarCarpeta(ruta);
            }
        } else if (conocida(baja, zip, conocidos, conservar)) {
            conserva = true;
        } else {
            deep_clean::logPaso("   borro " + ruta);
            actividad::detalle(actividad::texto("quitado", ruta));
            std::filesystem::remove(ruta, ec);
        }
    }
    return conserva;
}

// "Sys-Clk_Overlay" -> "sysclkoverlay": para reconocer la carpeta de /config de
// una app aunque se escriba distinto.
std::string clave(const std::string& nombre)
{
    std::string out;
    for (unsigned char c : nombre) {
        if (std::isalnum(c)) out += (char)std::tolower(c);
    }
    return out;
}

std::string sinExtension(const std::string& nombre)
{
    const auto punto = nombre.find_last_of('.');
    return punto == std::string::npos ? nombre : nombre.substr(0, punto);
}

// Iguales, o una empieza por la otra (sys-clk <-> sys-clk-overlay). Con un
// mínimo de 4 letras, para que un nombre corto no reconozca medio /config.
bool clavesCoinciden(const std::string& a, const std::string& b)
{
    return a.size() >= 4 && b.size() >= 4 && (a.rfind(b, 0) == 0 || b.rfind(a, 0) == 0);
}

// Recorre /switch (3 niveles). Borra la segunda copia de una app que el pack ya
// trae (mismo nombre dentro del .nro, otro archivo) y los .nro/.ovl vacíos o que
// no son un NRO. Lo demás es del usuario y se queda; sus nombres (carpetas y
// archivos) se guardan en `claves` para reconocer su carpeta de /config. Un .nro
// con el mismo nombre de archivo que uno del pack no cae: es donde la
// extracción lo va a actualizar.
void barrerSwitch(const std::string& carpeta, int profundidad, const DelZip& zip, const std::set<std::string>& conservar, std::vector<std::string>& claves)
{
    if (profundidad == 0) return;
    std::vector<std::string> nombres;
    for (const auto& n : nombresDe(carpeta)) {
        if (n.rfind("._", 0) != 0) nombres.push_back(n);
    }
    // Una carpeta con otros archivos además de apps es un port con sus datos
    // (o algo del usuario): no se toca nada de ella.
    const bool conDatos = carpeta != "/switch" && carpeta != "/switch/.overlays" &&
        std::any_of(nombres.begin(), nombres.end(), [&carpeta](const std::string& n) {
            return !esApp(minusculas(n)) && !esCarpeta(carpeta + "/" + n);
        });
    std::error_code ec;
    for (const auto& nombre : nombres) {
        const std::string ruta = carpeta + "/" + nombre;
        const std::string baja = minusculas(ruta);
        if (esCarpeta(ruta)) {
            claves.push_back(clave(nombre));
            barrerSwitch(ruta, profundidad - 1, zip, conservar, claves);
            continue;
        }
        if (!esApp(baja)) continue;
        claves.push_back(clave(sinExtension(nombre)));
        if (conDatos || protegida(baja, conservar) || zip.rutas.count(baja) || zip.archivosApp.count(minusculas(nombre))) continue;
        // Esta misma app, desde donde se haya abierto: no se borra a sí misma.
        if (baja == minusculas(util::getAppPath())) continue;
        FILE* f = std::fopen(ruta.c_str(), "rb");
        if (!f) continue;
        const LeerEn leer = lectorDeArchivo(f);
        const char* motivo = nullptr;
        if (!esNro(leer)) {
            motivo = "   borro (vacio o danado) ";
        } else {
            const std::string app = nombreApp(leer);
            if (!app.empty() && zip.apps.count(app)) motivo = "   borro (copia repetida) ";
        }
        std::fclose(f);
        if (motivo) {
            deep_clean::logPaso(motivo + ruta);
            actividad::detalle(actividad::texto("quitado", ruta));
            std::filesystem::remove(ruta, ec);
        }
    }
}

// /config: se conserva entera la carpeta de cada app que sigue en la SD, que
// trae el pack o que registra el catálogo (con los ajustes del usuario). Lo
// demás es de apps que ya no están.
void sincronizarConfig(const DelZip& zip, const Conocidos& conocidos, const std::set<std::string>& conservar, const std::vector<std::string>& claves)
{
    std::error_code ec;
    for (const auto& nombre : nombresDe("/config")) {
        const std::string ruta = "/config/" + nombre;
        if (nombre.rfind("._", 0) == 0) {
            std::filesystem::remove(ruta, ec);
            continue;
        }
        const std::string baja = minusculas(ruta);
        if (conocida(baja, zip, conocidos, conservar)) continue;
        if (esCarpeta(ruta)) {
            const std::string dentro = baja + "/";
            auto empiezaDentro = [&dentro](const std::string& r) { return r.rfind(dentro, 0) == 0; };
            if (std::any_of(zip.rutas.begin(), zip.rutas.end(), empiezaDentro) ||
                std::any_of(conocidos.rutas.begin(), conocidos.rutas.end(), empiezaDentro))
                continue;
            const std::string k = clave(nombre);
            if (std::any_of(claves.begin(), claves.end(), [&k](const std::string& c) { return clavesCoinciden(k, c); })) continue;
            deep_clean::logPaso("   borro dir " + ruta);
            actividad::detalle(actividad::texto("quitado", ruta));
            borrarCarpeta(ruta);
        } else {
            deep_clean::logPaso("   borro " + ruta);
            actividad::detalle(actividad::texto("quitado", ruta));
            std::filesystem::remove(ruta, ec);
        }
    }
}

// Quita las carpetas de /switch que quedaron vacías (o solo con "._" del
// Finder). Devuelve true si queda algo.
bool podarVacias(const std::string& carpeta, int profundidad)
{
    if (profundidad == 0) return true;
    const auto nombres = nombresDe(carpeta);
    bool quedaAlgo = false;
    std::error_code ec;
    for (const auto& nombre : nombres) {
        const std::string ruta = carpeta + "/" + nombre;
        if (nombre.rfind("._", 0) == 0) {
            std::filesystem::remove(ruta, ec);
            continue;
        }
        if (protegida(minusculas(ruta), {})) {
            quedaAlgo = true;
            continue;
        }
        if (esCarpeta(ruta)) {
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

void deep_clean::truncarLog()
{
    std::ofstream(std::string(CONFIG_PATH) + "deep_clean.log", std::ios::trunc);
}

void deep_clean::logPaso(const std::string& paso)
{
    // Segundos desde el primer paso de la sesion: asi el log muestra que
    // fase se come el tiempo cuando algo tarda mas de lo debido.
    static const auto inicio = std::chrono::steady_clock::now();
    const float s = std::chrono::duration<float>(std::chrono::steady_clock::now() - inicio).count();
    // Cada corrida de instalacion empieza el registro de cero: si no, la
    // ventana de log mostraria los restos de la corrida anterior.
    const bool nuevaCorrida = paso.rfind("=== inicio", 0) == 0;
    std::ofstream log(std::string(CONFIG_PATH) + "deep_clean.log", nuevaCorrida ? std::ios::trunc : std::ios::app);
    log << "[" << std::fixed << std::setprecision(1) << std::setw(6) << s << "s] " << paso << '\n';
}

void deep_clean::removeForeignPackLeftovers(const std::string& archivePath)
{
    // Registro de pasos en la SD: si algo vuelve a frenar la instalación,
    // este archivo dice exactamente dónde quedó (vive en una carpeta protegida).
    auto paso = [](const std::string& p) { deep_clean::logPaso(p); };

    paso("--- inicio deep_clean");
    actividad::paso(actividad::texto("buscando_restos"));
    std::set<std::string> conservar;
    for (const auto& c : fs::readLineByLine(FILES_IGNORE)) {
        if (!c.empty()) conservar.insert(minusculas(c[0] == '/' ? c : "/" + c));
    }
    const Conocidos conocidos = conocidosDelCatalogo();
    paso(conocidos.rutas.empty() && conocidos.nombres.empty() ? "catalogo: vacio" : "catalogo: ok");
    const DelZip zip = leerZip(archivePath);
    paso("zip: " + std::to_string(zip.rutas.size()) + " rutas, " + std::to_string(zip.apps.size()) + " apps");
    if (zip.rutas.empty()) {
        // Sin saber qué trae el pack, sincronizar /atmosphere lo vaciaría.
        paso("zip ilegible: no se limpia nada");
        return;
    }
    paso("sincronizando /atmosphere");
    sincronizar("/atmosphere", zip, conocidos, conservar);
    paso("barriendo /switch");
    std::vector<std::string> claves;
    barrerSwitch("/switch", 3, zip, conservar, claves);
    podarVacias("/switch", 3);
    for (const auto& n : zip.archivosApp) claves.push_back(clave(sinExtension(n)));
    for (const auto& n : zip.apps) claves.push_back(clave(n));
    for (const auto& n : conocidos.nombres) claves.push_back(clave(sinExtension(n)));
    paso("sincronizando /config");
    sincronizarConfig(zip, conocidos, conservar, claves);
    std::error_code ec;
    paso("raiz: payloads de otras versiones");
    {
        /* Un hekate de otra version (hekate_ctcaer_6.2.0.bin junto al del pack)
           y el fusee-primary/secondary de antes de Atmosphere 1.0 no los usa
           nada y confunden al elegir que payload lanzar. El de hekate solo si
           el pack trae el suyo; payload.bin, boot.dat y boot.ini no se tocan
           (los usan los chips). */
        const bool packTraeHekate = std::any_of(zip.rutas.begin(), zip.rutas.end(), [](const std::string& r) {
            return r.rfind("/hekate_ctcaer_", 0) == 0 && r.ends_with(".bin") && r.find('/', 1) == std::string::npos;
        });
        for (const auto& nombre : nombresDe("/")) {
            const std::string ruta = "/" + nombre;
            const std::string baja = minusculas(ruta);
            if (esCarpeta(ruta) || zip.rutas.count(baja) || protegida(baja, conservar)) continue;
            const bool hekateViejo = packTraeHekate && baja.rfind("/hekate_ctcaer_", 0) == 0 && baja.ends_with(".bin");
            const bool fuseeViejo = baja == "/fusee-primary.bin" || baja == "/fusee-secondary.bin";
            if (hekateViejo || fuseeViejo) {
                paso("   borro " + ruta);
                actividad::detalle(actividad::texto("quitado", ruta));
                std::filesystem::remove(ruta, ec);
            }
        }
    }
    for (const char* nombre : RAIZ_AJENA) {
        if (esCarpeta(std::string("/") + nombre)) {
            paso(std::string("   borro dir /") + nombre);
            borrarCarpeta(std::string("/") + nombre);
        }
    }
    /* Lo que deja macOS al copiar a la SD ("._" de AppleDouble, .DS_Store,
       .Spotlight-V100...): LayeredFS y algunos homebrew toman los "._" por
       archivos reales. Lo mismo que Herramientas > Limpiar archivos de Mac. */
    paso("basura de Mac");
    const limpiezaMac::Resultado mac = limpiezaMac::limpiar();
    paso("   " + std::to_string(mac.archivos) + " archivos y " + std::to_string(mac.carpetas) + " carpetas de Mac");
    paso("fin");
}

void deep_clean::quitarInisHuerfanos()
{
    /* bootloader/ini se conserva al instalar (ahi van las entradas que agrega
       el usuario), asi que la de otro pack seguia en el menu de hekate aunque
       lo que lanza ya no exista. Se quita solo el .ini en el que NINGUNA
       entrada puede arrancar: todas sus rutas payload=/fss0= faltan en la SD.
       Uno con al menos una entrada valida se deja entero. */
    std::set<std::string> conservar;
    for (const auto& c : fs::readLineByLine(FILES_IGNORE)) {
        if (!c.empty()) conservar.insert(minusculas(c[0] == '/' ? c : "/" + c));
    }
    std::error_code ec;
    for (const auto& nombre : nombresDe("/bootloader/ini")) {
        const std::string ruta = "/bootloader/ini/" + nombre;
        const std::string baja = minusculas(ruta);
        if (!baja.ends_with(".ini") || esCarpeta(ruta) || protegida(baja, conservar)) continue;
        std::ifstream ini(ruta);
        std::string linea;
        int rutas = 0, existen = 0;
        while (std::getline(ini, linea)) {
            while (!linea.empty() && (linea.back() == '\r' || linea.back() == ' ')) linea.pop_back();
            const auto igual = linea.find('=');
            if (igual == std::string::npos) continue;
            const std::string clave = minusculas(linea.substr(0, igual));
            if (clave != "payload" && clave != "fss0") continue;
            std::string valor = linea.substr(igual + 1);
            while (!valor.empty() && valor.front() == ' ') valor.erase(0, 1);
            if (valor.empty()) continue;
            ++rutas;
            if (std::filesystem::exists(valor[0] == '/' ? valor : "/" + valor, ec)) ++existen;
        }
        ini.close();
        if (rutas > 0 && existen == 0) {
            deep_clean::logPaso("   borro (entrada de hekate sin archivos) " + ruta);
            actividad::detalle(actividad::texto("quitado", ruta));
            std::filesystem::remove(ruta, ec);
        }
    }
}

// ---------------------------------------------------------------------------
// Instalacion limpia
// ---------------------------------------------------------------------------

namespace {

uint64_t tamano(const std::string& ruta)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(ruta, ec)) {
        const auto t = std::filesystem::file_size(ruta, ec);
        return ec ? 0 : t;
    }
    uint64_t total = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(ruta, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2)) {
            const auto t = it->file_size(e2);
            if (!e2) total += t;
        }
    }
    return total;
}

bool terminaEn(const std::string& s, const char* fin)
{
    const size_t n = std::strlen(fin);
    return s.size() >= n && s.compare(s.size() - n, n, fin) == 0;
}

// Algo del zip esta en esa ruta o debajo de ella (ruta en minusculas).
bool zipTraeAlgoEn(const DelZip& zip, const std::string& baja)
{
    if (zip.rutas.count(baja)) return true;
    const std::string dentro = baja + "/";
    const auto it = zip.rutas.lower_bound(dentro);
    return it != zip.rutas.end() && it->rfind(dentro, 0) == 0;
}

// En la raiz: lo que nunca se toca (en minusculas).
constexpr const char* RAIZ_INTOCABLE[] = {"nintendo", "emummc", "backup", "jksv", "atmosphere", "bootloader", "config", "switch",
                                          // Prelude / Nextendo dejan su estado aqui: borrarlo deja Nextendo a medias.
                                          "rootca.pem", "prelude_trace.txt", "prelude_exit.log"};
// Carpetas de mods en la raiz.
constexpr const char* RAIZ_MODS[] = {"ultimate", "mods", "saltysd"};

}  // namespace

deep_clean::estricta::Plan deep_clean::estricta::armar(const std::string& zipPath)
{
    Plan plan;
    deep_clean::logPaso("=== inicio plan instalacion limpia");
    std::set<std::string> conservar;
    for (const auto& c : fs::readLineByLine(FILES_IGNORE)) {
        if (!c.empty()) conservar.insert(minusculas(c[0] == '/' ? c : "/" + c));
    }
    const DelZip zip = leerZip(zipPath);
    if (zip.rutas.empty()) {
        deep_clean::logPaso("plan: zip ilegible");
        return plan;
    }
    const Conocidos conocidos = conocidosDelCatalogo();

    // Nombres de las apps del pack, para reconocer su carpeta de datos.
    std::vector<std::string> clavesPack;
    for (const auto& n : zip.archivosApp) clavesPack.push_back(clave(sinExtension(n)));
    for (const auto& n : zip.apps) clavesPack.push_back(clave(n));
    for (const auto& r : zip.rutas) {
        if (r.rfind("/switch/", 0) == 0) {
            const auto fin = r.find('/', 8);
            if (fin != std::string::npos) clavesPack.push_back(clave(r.substr(8, fin - 8)));
        }
    }
    auto esDelPack = [&clavesPack](const std::string& nombre) {
        const std::string k = clave(nombre);
        return std::any_of(clavesPack.begin(), clavesPack.end(), [&k](const std::string& c) { return clavesCoinciden(k, c); });
    };
    const std::string propia = minusculas(util::getAppPath());

    // Las apps ajenas que entran al plan, con su categoria final (APPS o PORTS):
    // su carpeta de /config sigue a la app.
    std::vector<std::pair<std::string, Categoria>> clavesApps;
    auto agrega = [&plan](const std::string& ruta, Categoria cat) {
        Elemento e;
        e.ruta = ruta;
        e.bytes = tamano(ruta);
        // Una app ajena grande es un port o un emulador con los juegos del usuario.
        if (cat == APPS && e.bytes >= (256ull << 20)) cat = PORTS;
        e.cat = cat;
        plan.bytes[cat] += e.bytes;
        plan.cuenta[cat] += 1;
        plan.elementos.push_back(std::move(e));
        return cat;
    };
    // Nunca: las llaves de la consola (prod.keys, title.keys) y lo de Nextendo/Prelude.
    auto intocable = [](const std::string& baja) {
        const std::string nombre = archivoDe(baja);
        return terminaEn(nombre, ".keys") || nombre.find("prelude") != std::string::npos || nombre.find("nextendo") != std::string::npos;
    };

    // /switch: lo que no trae el pack.
    for (const auto& nombre : nombresDe("/switch")) {
        const std::string ruta = "/switch/" + nombre;
        const std::string baja = minusculas(ruta);
        if (nombre.rfind("._", 0) == 0) {
            agrega(ruta, RESTOS);
            continue;
        }
        if (protegida(baja, conservar) || intocable(baja)) continue;
        if (propia == baja || propia.rfind(baja + "/", 0) == 0) continue;
        if (baja == "/switch/.overlays") {
            for (const auto& ovl : nombresDe(ruta)) {
                const std::string r = ruta + "/" + ovl;
                const std::string b = minusculas(r);
                if (esApp(b) && !zip.rutas.count(b) && !protegida(b, conservar) && !intocable(b))
                    clavesApps.emplace_back(clave(sinExtension(ovl)), agrega(r, APPS));
            }
            continue;
        }
        // Otras carpetas ocultas (.packages es la base de datos de HB App Store, que va en el pack).
        if (nombre[0] == '.') continue;
        if (zipTraeAlgoEn(zip, baja)) continue;
        if (esCarpeta(ruta)) {
            // La carpeta de datos de una app del pack (Checkpoint guarda ahi las partidas).
            if (esDelPack(nombre)) continue;
            clavesApps.emplace_back(clave(nombre), agrega(ruta, APPS));
            continue;
        }
        // Un .nro suelto: si es otra copia de una app del pack, es un resto.
        bool repetida = false;
        if (esApp(baja)) {
            if (FILE* f = std::fopen(ruta.c_str(), "rb")) {
                const std::string app = nombreApp(lectorDeArchivo(f));
                std::fclose(f);
                repetida = !app.empty() && zip.apps.count(app);
            }
        }
        const Categoria catFinal = agrega(ruta, repetida ? RESTOS : APPS);
        if (esApp(baja)) clavesApps.emplace_back(clave(sinExtension(nombre)), catFinal);
    }

    // /config: la carpeta de una app ajena sigue a esa app; la de nadie es un resto.
    for (const auto& nombre : nombresDe("/config")) {
        const std::string ruta = "/config/" + nombre;
        const std::string baja = minusculas(ruta);
        if (protegida(baja, conservar) || zipTraeAlgoEn(zip, baja)) continue;
        if (baja == "/config/aio-switch-updater") continue;
        if (esCarpeta(ruta) && esDelPack(nombre)) continue;
        const std::string k = clave(nombre);
        const auto app = std::find_if(clavesApps.begin(), clavesApps.end(), [&k](const auto& c) { return clavesCoinciden(k, c.first); });
        agrega(ruta, app == clavesApps.end() ? RESTOS : app->second);
    }

    // Descargas viejas de esta app (el pack recien bajado se borra al terminar).
    for (const auto& nombre : nombresDe(DOWNLOAD_PATH)) {
        const std::string ruta = std::string(DOWNLOAD_PATH) + nombre;
        if (terminaEn(minusculas(nombre), ".zip") && ruta != AMS_FILENAME && !esCarpeta(ruta)) agrega(ruta, RESTOS);
    }

    // /themes: los temas que no trae el pack.
    for (const auto& nombre : nombresDe("/themes")) {
        const std::string ruta = "/themes/" + nombre;
        const std::string baja = minusculas(ruta);
        if (zipTraeAlgoEn(zip, baja) || protegida(baja, conservar)) continue;
        agrega(ruta, nombre.rfind("._", 0) == 0 ? RESTOS : TEMAS);
    }

    // La raiz.
    for (const auto& nombre : nombresDe("/")) {
        const std::string ruta = "/" + nombre;
        const std::string baja = minusculas(nombre);
        if (std::any_of(std::begin(RAIZ_INTOCABLE), std::end(RAIZ_INTOCABLE), [&baja](const char* n) { return baja == n; })) continue;
        if (baja == "themes" || zipTraeAlgoEn(zip, "/" + baja) || protegida("/" + baja, conservar) || intocable("/" + baja)) continue;
        // Juegos sueltos: son del usuario, no de un pack.
        if (terminaEn(baja, ".nsp") || terminaEn(baja, ".nsz") || terminaEn(baja, ".xci") || terminaEn(baja, ".xcz")) continue;
        if (std::any_of(std::begin(RAIZ_MODS), std::end(RAIZ_MODS), [&baja](const char* n) { return baja == n; })) {
            agrega(ruta, MODS);
            continue;
        }
        if (baja[0] == '.') {  // .Spotlight-V100, .fseventsd, .mesa...
            agrega(ruta, RESTOS);
            continue;
        }
        // Datos en la raiz de una app del catalogo que ya no esta en el pack (tico/).
        const std::string dentro = "/" + baja + "/";
        const bool deCatalogo = std::any_of(conocidos.rutas.begin(), conocidos.rutas.end(), [&dentro](const std::string& r) { return r.rfind(dentro, 0) == 0; });
        agrega(ruta, deCatalogo ? APPS : RAIZ);
    }

    std::sort(plan.elementos.begin(), plan.elementos.end(), [](const Elemento& a, const Elemento& b) {
        return a.cat != b.cat ? a.cat < b.cat : a.bytes > b.bytes;
    });
    s64 total = 0, libre = 0;
    if (R_SUCCEEDED(nsGetTotalSpaceSize(NcmStorageId_SdCard, &total)) && R_SUCCEEDED(nsGetFreeSpaceSize(NcmStorageId_SdCard, &libre)) && total > libre)
        plan.usado = (uint64_t)(total - libre);
    plan.valido = true;
    deep_clean::logPaso("plan: " + std::to_string(plan.elementos.size()) + " elementos");
    return plan;
}

uint64_t deep_clean::estricta::aBorrar(const Plan& plan, const Opciones& op)
{
    uint64_t total = 0;
    for (int c = 0; c < N_CATEGORIAS; ++c) {
        if (c == RESTOS || !op.conservar[c]) total += plan.bytes[c];
    }
    return total;
}

int deep_clean::estricta::ejecutar(const Plan& plan, const Opciones& op)
{
    static const char* NOMBRES[] = {"resto", "app", "mods", "tema", "raiz", "port"};
    std::ofstream log(std::string(CONFIG_PATH) + "limpieza.log", std::ios::trunc);
    log << "Instalacion limpia: lo que se quito antes de instalar el pack\n";
    int quitadas = 0;
    std::error_code ec;
    for (const auto& e : plan.elementos) {
        if (e.cat != RESTOS && op.conservar[e.cat]) continue;
        log << "[" << NOMBRES[e.cat] << "] " << e.ruta << " (" << e.bytes << " B)\n";
        log.flush();
        actividad::detalle(actividad::texto("quitado", e.ruta));
        if (esCarpeta(e.ruta))
            borrarCarpeta(e.ruta);
        else
            std::filesystem::remove(e.ruta, ec);
        ++quitadas;
    }
    deep_clean::logPaso("instalacion limpia: " + std::to_string(quitadas) + " quitadas");
    return quitadas;
}
