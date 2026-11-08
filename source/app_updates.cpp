#include "app_updates.hpp"

#include <minizip/unzip.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <filesystem>
#include <vector>

#include "constants.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "utils.hpp"

namespace appUpdates {

    namespace {

        // La version que el autor puso dentro del .nro (display_version del NACP).
        std::string leerNroVersion(const std::string& path);

        // Con una LecturaSwitch activa (armando una ventana), cada .nro se lee
        // una vez: la misma app se pregunta desde varias pestañas.
        std::string nroVersion(const std::string& path)
        {
            static std::map<std::string, std::string> memo;
            static int memoGen = 0;
            const int gen = util::generacionLectura();
            if (!gen)
                return leerNroVersion(path);
            if (gen != memoGen) {
                memo.clear();
                memoGen = gen;
            }
            const auto it = memo.find(path);
            if (it != memo.end())
                return it->second;
            const std::string v = util::existeEnSd(path) ? leerNroVersion(path) : "";
            memo[path] = v;
            return v;
        }

        std::string leerNroVersion(const std::string& path)
        {
            FILE* f = std::fopen(path.c_str(), "rb");
            if (!f)
                return "";
            std::string out;
            uint8_t head[0x20], aset[0x38];
            char version[0x11] = {};
            uint32_t end = 0;
            uint64_t start = 0, size = 0;
            if (std::fread(head, 1, sizeof(head), f) == sizeof(head)) {
                std::memcpy(&end, head + 0x18, 4);
                if (std::fseek(f, end, SEEK_SET) == 0 && std::fread(aset, 1, sizeof(aset), f) == sizeof(aset) && std::memcmp(aset, "ASET", 4) == 0) {
                    std::memcpy(&start, aset + 0x18, 8);
                    std::memcpy(&size, aset + 0x20, 8);
                    if (size >= 0x3070 && std::fseek(f, end + start + 0x3060, SEEK_SET) == 0 && std::fread(version, 1, 0x10, f) == 0x10)
                        out = version;
                }
            }
            std::fclose(f);
            return out;
        }

        // "v1.2.0" > "1.1.9": por numeros, como el gestor de homebrew de HomeNX.
        bool isNewer(const std::string& published, const std::string& installed)
        {
            auto parts = [](const std::string& v) {
                std::vector<long> out;
                long cur = -1;
                for (char c : v) {
                    if (std::isdigit(static_cast<unsigned char>(c)))
                        cur = (cur < 0 ? 0 : cur * 10) + (c - '0');
                    else if (cur >= 0) {
                        out.push_back(cur);
                        cur = -1;
                    }
                }
                if (cur >= 0)
                    out.push_back(cur);
                return out;
            };
            const auto a = parts(published), b = parts(installed);
            if (a.empty() || b.empty())
                return published != installed;
            for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
                const long x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
                if (x != y)
                    return x > y;
            }
            return false;
        }

        const nlohmann::ordered_json* entry(const nlohmann::ordered_json& nxlinks, const std::string& url)
        {
            const auto all = nxlinks.find(NXLINKS_KEY);
            if (all == nxlinks.end() || !all->is_object())
                return nullptr;
            const auto app = all->find(url);
            return app != all->end() && app->is_object() ? &*app : nullptr;
        }

        std::string text(const nlohmann::ordered_json& j, const char* key)
        {
            const auto v = j.find(key);
            return v != j.end() && v->is_string() ? v->get<std::string>() : "";
        }

        std::atomic<int> instalaciones{0};

        // El JPEG del icono de un .nro: la seccion icon de su ASET.
        std::vector<uint8_t> leerIconoNro(const std::string& path)
        {
            std::vector<uint8_t> out;
            FILE* f = std::fopen(path.c_str(), "rb");
            if (!f)
                return out;
            uint8_t head[0x20], aset[0x18];
            uint32_t end = 0;
            uint64_t start = 0, size = 0;
            if (std::fread(head, 1, sizeof(head), f) == sizeof(head)) {
                std::memcpy(&end, head + 0x18, 4);
                if (std::fseek(f, end, SEEK_SET) == 0 && std::fread(aset, 1, sizeof(aset), f) == sizeof(aset) && std::memcmp(aset, "ASET", 4) == 0) {
                    std::memcpy(&start, aset + 0x8, 8);
                    std::memcpy(&size, aset + 0x10, 8);
                    if (size > 0 && size < (1u << 20) && std::fseek(f, end + start, SEEK_SET) == 0) {
                        out.resize(size);
                        if (std::fread(out.data(), 1, size, f) != size)
                            out.clear();
                    }
                }
            }
            std::fclose(f);
            return out;
        }

        // Copia en memoria del ultimo catalogo consultado, para consultas
        // que ocurren lejos del arranque (la limpieza profunda del pack)
        // sin volver a tocar la red.
        nlohmann::ordered_json& catalogo()
        {
            static nlohmann::ordered_json j;
            return j;
        }

    }  // namespace

    nlohmann::ordered_json fetchPublished()
    {
        nlohmann::ordered_json res;
        download::getRequest(APP_VERSIONS_URL, res);
        catalogo() = res.is_object() ? res : nlohmann::ordered_json::object();
        return catalogo();
    }

    const nlohmann::ordered_json& catalogoGuardado()
    {
        return catalogo();
    }

    std::string published(const nlohmann::ordered_json& nxlinks, const std::string& url)
    {
        const nlohmann::ordered_json* app = entry(nxlinks, url);
        return app ? text(*app, "fecha") : "";
    }

    bool hasUpdate(const nlohmann::ordered_json& nxlinks, const nlohmann::ordered_json& installed, const std::string& url)
    {
        const nlohmann::ordered_json* app = entry(nxlinks, url);
        if (!app)
            return false;

        /* Si en la SD hay un .nro de la app, manda su version interna: sirve
           aunque se haya instalado por otro lado. */
        bool compared = false;
        const std::string nro = text(*app, "nro_version");
        if (!nro.empty() && url.size() > 4 && url.substr(url.size() - 4) == ".nro") {
            const std::string path = util::resolveNroDestination(url.substr(url.rfind('/') + 1));
            if (util::existeEnSd(path)) {
                const std::string have = nroVersion(path);
                if (!have.empty()) {
                    compared = true;
                    if (isNewer(nro, have))
                        return true;
                }
            }
        }
        const auto nros = app->find("nros");
        if (nros != app->end() && nros->is_array()) {
            for (const auto& n : *nros) {
                if (!n.is_object())
                    continue;
                const std::string path = text(n, "ruta"), version = text(n, "version");
                const std::string have = path.empty() ? "" : nroVersion(path);
                if (!have.empty() && !version.empty()) {
                    compared = true;
                    if (isNewer(version, have))
                        return true;
                }
            }
        }
        if (compared)
            return false;

        // Sin .nro que leer (sysmodules, zips sin .nro): la fecha anotada al instalar.
        const auto anotada = installed.find(url);
        if (anotada == installed.end() || !anotada->is_string() || anotada->get<std::string>().empty())
            return false;
        const std::string publicada = text(*app, "fecha");
        return !publicada.empty() && publicada != anotada->get<std::string>();
    }

    std::string installedVersion(const std::string& url)
    {
        if (url.size() <= 4 || url.substr(url.size() - 4) != ".nro")
            return "";
        const std::string path = util::resolveNroDestination(url.substr(url.rfind('/') + 1));
        return util::existeEnSd(path) ? nroVersion(path) : "";
    }

    std::string publishedVersion(const nlohmann::ordered_json& nxlinks, const std::string& url)
    {
        const nlohmann::ordered_json* app = entry(nxlinks, url);
        return app ? text(*app, "nro_version") : "";
    }

    bool isInstalled(const nlohmann::ordered_json& nxlinks, const std::string& url)
    {
        std::error_code ec;
        if (url.size() > 4 && url.substr(url.size() - 4) == ".nro")
            return util::existeEnSd(util::resolveNroDestination(url.substr(url.rfind('/') + 1)));
        const nlohmann::ordered_json* app = entry(nxlinks, url);
        if (!app)
            return false;
        const auto nros = app->find("nros");
        if (nros != app->end() && nros->is_array()) {
            for (const auto& n : *nros) {
                if (n.is_object() && util::existeEnSd(text(n, "ruta")))
                    return true;
            }
        }
        const auto marcas = app->find("marcas");
        if (marcas != app->end() && marcas->is_array()) {
            for (const auto& m : *marcas) {
                if (m.is_string() && util::existeEnSd(m.get<std::string>()))
                    return true;
            }
        }
        return false;
    }

    nlohmann::ordered_json installed()
    {
        // Armando una ventana (LecturaSwitch activa) se lee una sola vez:
        // varias pestanas lo piden y cada lectura es un archivo de la SD.
        static nlohmann::ordered_json memo;
        static int memoGen = 0;
        const int gen = util::generacionLectura();
        if (gen && gen == memoGen)
            return memo;
        nlohmann::ordered_json registro = fs::parseJsonFile(APP_VERSIONS_PATH);
        if (!registro.is_object())
            registro = nlohmann::ordered_json::object();
        if (gen) {
            memo = registro;
            memoGen = gen;
        }
        return registro;
    }

    int generacionInstalaciones()
    {
        return instalaciones.load();
    }

    void asegurarIconos(const nlohmann::ordered_json& catalogo)
    {
        if (!catalogo.is_object())
            return;
        std::error_code ec;
        bool falta = false;
        for (const auto& [url, app] : catalogo.items()) {
            if (!app.is_object())
                continue;
            const std::string nombre = text(app, "icono");
            if (!nombre.empty() && !std::filesystem::exists(std::string(ICONOS_PATH) + nombre, ec)) {
                falta = true;
                break;
            }
        }
        if (!falta)
            return;

        const std::string zip = std::string(ICONOS_PATH) + "iconos.zip.tmp";
        std::filesystem::create_directories(ICONOS_PATH, ec);
        if (download::downloadFile(ICONOS_URL, zip, OFF) != 200) {
            std::filesystem::remove(zip, ec);
            return;
        }
        unzFile z = unzOpen(zip.c_str());
        if (z) {
            for (int r = unzGoToFirstFile(z); r == UNZ_OK; r = unzGoToNextFile(z)) {
                char nombre[128] = {};
                unz_file_info info{};
                if (unzGetCurrentFileInfo(z, &info, nombre, sizeof(nombre) - 1, nullptr, 0, nullptr, 0) != UNZ_OK)
                    continue;
                const std::string n = nombre;
                // Solo "<hash>.jpg" sueltos: nada de rutas que salgan de la carpeta.
                if (n.size() < 5 || n.find('/') != std::string::npos || n.find('\\') != std::string::npos || n.substr(n.size() - 4) != ".jpg" ||
                    info.uncompressed_size == 0 || info.uncompressed_size > (1u << 20))
                    continue;
                const std::string destino = std::string(ICONOS_PATH) + n;
                if (std::filesystem::exists(destino, ec) || unzOpenCurrentFile(z) != UNZ_OK)
                    continue;
                std::vector<char> datos(info.uncompressed_size);
                const int leidos = unzReadCurrentFile(z, datos.data(), datos.size());
                unzCloseCurrentFile(z);
                if (leidos != static_cast<int>(datos.size()))
                    continue;
                // tmp + rename: un apagon a mitad no deja un icono roto.
                const std::string tmp = destino + ".tmp";
                if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
                    const bool ok = std::fwrite(datos.data(), 1, datos.size(), f) == datos.size();
                    std::fclose(f);
                    if (ok)
                        std::filesystem::rename(tmp, destino, ec);
                    else
                        std::filesystem::remove(tmp, ec);
                }
            }
            unzClose(z);
        }
        std::filesystem::remove(zip, ec);
    }

    std::string rutaIcono(const nlohmann::ordered_json& nxlinks, const std::string& url)
    {
        const nlohmann::ordered_json* app = entry(nxlinks, url);
        const std::string nombre = app ? text(*app, "icono") : "";
        if (nombre.empty())
            return "";
        const std::string ruta = std::string(ICONOS_PATH) + nombre;
        std::error_code ec;
        return std::filesystem::exists(ruta, ec) ? ruta : "";
    }

    std::vector<uint8_t> iconoInstalado(const nlohmann::ordered_json& nxlinks, const std::string& url)
    {
        if (url.size() > 4 && url.substr(url.size() - 4) == ".nro") {
            const std::string path = util::resolveNroDestination(url.substr(url.rfind('/') + 1));
            return util::existeEnSd(path) ? leerIconoNro(path) : std::vector<uint8_t>{};
        }
        const nlohmann::ordered_json* app = entry(nxlinks, url);
        if (!app)
            return {};
        const auto nros = app->find("nros");
        if (nros == app->end() || !nros->is_array())
            return {};
        // El de /switch/ es el de la app; los demas suelen ser modulos.
        for (int pasada = 0; pasada < 2; ++pasada) {
            for (const auto& n : *nros) {
                const std::string ruta = n.is_object() ? text(n, "ruta") : "";
                if (ruta.empty() || (pasada == 0 && ruta.rfind("/switch/", 0) != 0) || !util::existeEnSd(ruta))
                    continue;
                std::vector<uint8_t> icono = leerIconoNro(ruta);
                if (!icono.empty())
                    return icono;
            }
        }
        return {};
    }

    void recordInstalled(const std::string& url, const std::string& date)
    {
        instalaciones.fetch_add(1);
        if (url.empty() || date.empty())
            return;
        nlohmann::ordered_json registro = installed();
        registro[url] = date;
        fs::writeJsonToFile(registro, APP_VERSIONS_PATH);
    }

}  // namespace appUpdates
