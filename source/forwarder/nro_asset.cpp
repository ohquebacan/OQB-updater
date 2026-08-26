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

}  // namespace

Result configFromNro(const std::string& nro_path, Config& out)
{
    FILE* raw = std::fopen(nro_path.c_str(), "rb");
    if (!raw) return Result_NroInvalid;
    FileCloser closer{raw};

    NroStart start{};
    NroHeader header{};
    if (!readAt(raw, 0, &start, sizeof(start)) || !readAt(raw, sizeof(start), &header, sizeof(header))) {
        return Result_NroInvalid;
    }
    if (header.magic != NROHEADER_MAGIC) {
        return Result_NroInvalid;
    }

    out.nro_path = nro_path;

    // El asset header, si existe, va justo después del cuerpo del NRO.
    NroAssetHeader asset{};
    const bool has_assets =
        readAt(raw, header.size, &asset, sizeof(asset)) &&
        asset.magic == NROASSETHEADER_MAGIC &&
        asset.version <= NROASSETHEADER_VERSION;

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
        if (!readAt(raw, header.size + asset.icon.offset, out.icon.data(), out.icon.size())) {
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
