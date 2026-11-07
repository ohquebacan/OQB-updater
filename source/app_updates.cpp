#include "app_updates.hpp"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "constants.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "utils.hpp"

namespace appUpdates {

    namespace {

        // La version que el autor puso dentro del .nro (display_version del NACP).
        std::string nroVersion(const std::string& path)
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
            std::error_code ec;
            if (std::filesystem::exists(path, ec)) {
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

    bool isInstalled(const nlohmann::ordered_json& nxlinks, const std::string& url)
    {
        std::error_code ec;
        if (url.size() > 4 && url.substr(url.size() - 4) == ".nro")
            return std::filesystem::exists(util::resolveNroDestination(url.substr(url.rfind('/') + 1)), ec);
        const nlohmann::ordered_json* app = entry(nxlinks, url);
        if (!app)
            return false;
        const auto nros = app->find("nros");
        if (nros != app->end() && nros->is_array()) {
            for (const auto& n : *nros) {
                if (n.is_object() && std::filesystem::exists(text(n, "ruta"), ec))
                    return true;
            }
        }
        const auto marcas = app->find("marcas");
        if (marcas != app->end() && marcas->is_array()) {
            for (const auto& m : *marcas) {
                if (m.is_string() && std::filesystem::exists(m.get<std::string>(), ec))
                    return true;
            }
        }
        return false;
    }

    nlohmann::ordered_json installed()
    {
        nlohmann::ordered_json registro = fs::parseJsonFile(APP_VERSIONS_PATH);
        return registro.is_object() ? registro : nlohmann::ordered_json::object();
    }

    void recordInstalled(const std::string& url, const std::string& date)
    {
        if (url.empty() || date.empty())
            return;
        nlohmann::ordered_json registro = installed();
        registro[url] = date;
        fs::writeJsonToFile(registro, APP_VERSIONS_PATH);
    }

}  // namespace appUpdates
