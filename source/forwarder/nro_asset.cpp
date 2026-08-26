// Lee el asset section de un NRO (icono JPEG + NACP) para armar el forwarder.
#include <switch.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

#include "forwarder.hpp"
#include "forwarder/result.hpp"

namespace fwd {
namespace {

    // Icono de reserva para NROs sin assets propios. El menú HOME sólo acepta
    // JPEG de 256x256, así que no sirve reutilizar el png de la interfaz.
    constexpr const char FALLBACK_ICON[] = "romfs:/forwarder_icon.jpg";

    struct FileCloser
    {
        FILE* f;
        ~FileCloser()
        {
            if (f) std::fclose(f);
        }
    };

    bool readAt(FILE* f, s64 offset, void* out, size_t size)
    {
        if (std::fseek(f, offset, SEEK_SET) != 0) return false;
        return std::fread(out, 1, size, f) == size;
    }

    // "JKSV.nro" -> "JKSV"
    std::string nameFromPath(const std::string& path)
    {
        return std::filesystem::path(path).stem().string();
    }

    // El menú HOME sólo muestra bien un JPEG de 256x256; con cualquier otra
    // cosa dibuja un mosaico roto. Los NROs pueden traer el icono que quieran,
    // así que hay que mirarlo antes de meterlo en el NCA de control.
    bool isValidHomeMenuIcon(const std::vector<u8>& icon)
    {
        // SOI
        if (icon.size() < 4 || icon[0] != 0xFF || icon[1] != 0xD8) {
            return false;
        }

        size_t i = 2;
        while (i + 9 < icon.size()) {
            if (icon[i] != 0xFF) return false;

            const u8 marker = icon[i + 1];
            const u16 len = (icon[i + 2] << 8) | icon[i + 3];
            if (len < 2) return false;

            // SOF0/1/2: acá vienen alto y ancho.
            if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
                const u16 h = (icon[i + 5] << 8) | icon[i + 6];
                const u16 w = (icon[i + 7] << 8) | icon[i + 8];
                return w == 256 && h == 256;
            }

            i += 2 + len;
        }

        return false;
    }

    // Deja el archivo posicionado y devuelve el asset header si el NRO trae uno.
    bool readNroHeaders(FILE* f, NroHeader& header, NroAssetHeader& asset)
    {
        NroStart start{};
        if (!readAt(f, 0, &start, sizeof(start)) || !readAt(f, sizeof(start), &header, sizeof(header))) {
            return false;
        }
        if (header.magic != NROHEADER_MAGIC) {
            return false;
        }

        // El asset header, si existe, va justo después del cuerpo del NRO.
        return readAt(f, header.size, &asset, sizeof(asset)) &&
               asset.magic == NROASSETHEADER_MAGIC &&
               asset.version <= NROASSETHEADER_VERSION;
    }

}  // namespace

std::string nameFromNro(const std::string& nro_path)
{
    FILE* raw = std::fopen(nro_path.c_str(), "rb");
    if (!raw) return nameFromPath(nro_path);
    FileCloser closer{raw};

    NroHeader header{};
    NroAssetHeader asset{};
    if (readNroHeaders(raw, header, asset) && asset.nacp.size >= sizeof(NacpStruct)) {
        NacpStruct nacp{};
        if (readAt(raw, header.size + asset.nacp.offset, &nacp, sizeof(nacp))) {
            NacpLanguageEntry* entry = nullptr;
            if (R_SUCCEEDED(nacpGetLanguageEntry(&nacp, &entry)) && entry && entry->name[0]) {
                return entry->name;
            }
        }
    }

    return nameFromPath(nro_path);
}

Result configFromNro(const std::string& nro_path, Config& out)
{
    FILE* raw = std::fopen(nro_path.c_str(), "rb");
    if (!raw) return Result_NroInvalid;
    FileCloser closer{raw};

    NroHeader header{};
    NroAssetHeader asset{};
    const bool has_assets = readNroHeaders(raw, header, asset);
    if (header.magic != NROHEADER_MAGIC) {
        return Result_NroInvalid;
    }

    out.nro_path = nro_path;

    bool got_nacp = false;
    if (has_assets && asset.nacp.size >= sizeof(NacpStruct)) {
        if (readAt(raw, header.size + asset.nacp.offset, &out.nacp, sizeof(out.nacp))) {
            got_nacp = true;
        }
    }

    if (got_nacp) {
        NacpLanguageEntry* entry = nullptr;
        if (R_SUCCEEDED(nacpGetLanguageEntry(&out.nacp, &entry)) && entry) {
            out.name = entry->name;
            out.author = entry->author;
        }
    }
    else {
        out.nacp = {};
    }

    if (out.name.empty()) {
        out.name = nameFromPath(nro_path);
    }
    if (out.author.empty()) {
        out.author = "Homebrew";
    }

    if (has_assets && asset.icon.size > 0) {
        out.icon.resize(asset.icon.size);
        if (!readAt(raw, header.size + asset.icon.offset, out.icon.data(), out.icon.size()) ||
            !isValidHomeMenuIcon(out.icon)) {
            // Mejor el icono genérico que un mosaico roto en el menú HOME.
            out.icon.clear();
        }
    }

    if (out.icon.empty()) {
        FILE* fallback = std::fopen(FALLBACK_ICON, "rb");
        if (!fallback) return Result_NroNoAssets;
        FileCloser fallback_closer{fallback};

        std::fseek(fallback, 0, SEEK_END);
        const auto size = std::ftell(fallback);
        if (size <= 0) return Result_NroNoAssets;

        out.icon.resize(size);
        if (!readAt(fallback, 0, out.icon.data(), out.icon.size())) {
            return Result_NroNoAssets;
        }
    }

    return 0;
}

}  // namespace fwd
