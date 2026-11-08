#include "limpieza_mac.hpp"

#include <borealis.hpp>

#include <cstdio>
#include <filesystem>
#include <string>

#include "actividad.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {
    constexpr const char* SIN_REVISAR[] = {"Nintendo", "emuMMC", "emummc"};
    constexpr const char* CARPETAS_RAIZ[] = {".Spotlight-V100", ".fseventsd", ".Trashes", ".TemporaryItems"};

    bool esAppleDouble(const std::string& ruta)
    {
        unsigned char cab[4] = {};
        FILE* f = std::fopen(ruta.c_str(), "rb");
        if (!f)
            return false;
        const bool leido = std::fread(cab, 1, 4, f) == 4;
        std::fclose(f);
        return leido && cab[0] == 0x00 && cab[1] == 0x05 && cab[2] == 0x16 && cab[3] == 0x07;
    }

    long long tamanoDe(const std::filesystem::path& p)
    {
        std::error_code ec;
        long long total = 0;
        if (std::filesystem::is_directory(p, ec)) {
            for (auto it = std::filesystem::recursive_directory_iterator(p, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (it->is_regular_file(ec))
                    total += static_cast<long long>(it->file_size(ec));
            }
            return total;
        }
        const auto t = std::filesystem::file_size(p, ec);
        return ec ? 0 : static_cast<long long>(t);
    }

    void recorrer(const std::string& carpeta, int nivel, limpiezaMac::Resultado& r)
    {
        std::error_code ec;
        std::filesystem::directory_iterator it(carpeta, ec);
        if (ec)
            return;
        if (nivel <= 1)
            actividad::paso(actividad::texto("revisando", carpeta));
        for (; it != std::filesystem::directory_iterator(); it.increment(ec)) {
            if (ec)
                break;
            const std::string nombre = it->path().filename().string();
            const std::string ruta = (carpeta == "/" ? "/" : carpeta + "/") + nombre;
            if (it->is_symlink(ec))
                continue;
            if (it->is_directory(ec)) {
                bool saltar = false;
                if (nivel == 0) {
                    for (const char* s : SIN_REVISAR)
                        saltar = saltar || nombre == s;
                    for (const char* c : CARPETAS_RAIZ) {
                        if (nombre == c) {
                            const long long t = tamanoDe(ruta);
                            std::filesystem::remove_all(ruta, ec);
                            if (!std::filesystem::exists(ruta, ec)) {
                                ++r.carpetas;
                                r.bytes += t;
                                actividad::detalle(actividad::texto("quitado", ruta));
                            }
                            saltar = true;
                        }
                    }
                }
                if (!saltar)
                    recorrer(ruta, nivel + 1, r);
                continue;
            }
            const bool dsStore = nombre == ".DS_Store";
            const bool punto = nombre.rfind("._", 0) == 0 && esAppleDouble(ruta);
            if (!dsStore && !punto)
                continue;
            const long long t = tamanoDe(ruta);
            std::filesystem::remove(ruta, ec);
            if (!std::filesystem::exists(ruta, ec)) {
                ++r.archivos;
                r.bytes += t;
            }
        }
    }

    std::string unidades(long long b)
    {
        if (b < 1024)
            return fmt::format("{} B", b);
        if (b < 1024 * 1024)
            return fmt::format("{:.0f} KB", b / 1024.0);
        return fmt::format("{:.1f} MB", b / (1024.0 * 1024.0));
    }
}  // namespace

limpiezaMac::Resultado limpiezaMac::limpiar()
{
    Resultado r;
    recorrer("/", 0, r);
    return r;
}

std::string limpiezaMac::resumen(const Resultado& r)
{
    if (r.archivos == 0 && r.carpetas == 0)
        return "menus/tools/clean_mac_nada"_i18n;
    return fmt::format("menus/tools/clean_mac_listo"_i18n, r.archivos, r.carpetas, unidades(r.bytes));
}
