#include "limpiar_emunand.hpp"

#include <borealis.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>

#include "actividad.hpp"
#include "home_rotos.hpp"

namespace {
    constexpr const char LOG[] = "/config/aio-switch-updater/limpiar_emunand.log";
    constexpr u64 MASCARA = ~0x1FFFULL;  // juego, actualización y DLC como uno

    // Juegos de la tienda y ports/homebrew empaquetados como juego (05…).
    bool esJuego(u64 id)
    {
        return (id >= 0x0100000000010000ULL && id < 0x0200000000000000ULL) || (id >> 56) == 0x05;
    }

    std::string hex(u64 v) { return fmt::format("{:016X}", v); }

    // Lo que devuelve el SMC GetEmummcConfig de Atmosphère en la página del usuario.
    struct alignas(0x1000) RutasEmummc {
        char archivo[0x80];
        char nintendo[0x80];
    };
    RutasEmummc g_rutas;

    /* Pregunta al monitor seguro con qué se arrancó. La carpeta Nintendo tiene
       que estar dentro de emummc/ (la propia de la emuNAND); si no, no se
       limpia nada. */
    limpiarEmunand::Donde carpetaNintendo(std::string& ruta)
    {
        using limpiarEmunand::Donde;
        ruta.clear();
        u64 tipo = 0;
        if (R_FAILED(splInitialize())) return Donde::NoSeSabe;
        const Result rc = splGetConfig(static_cast<SplConfigItem>(65007), &tipo);
        splExit();
        if (R_FAILED(rc)) return Donde::NoSeSabe;
        if (tipo == 0) return Donde::SysNand;

        std::memset(&g_rutas, 0, sizeof(g_rutas));
        SecmonArgs args{};
        args.X[0] = 0xF0000404;  // GetEmummcConfig
        args.X[1] = 0;           // NAND
        args.X[2] = reinterpret_cast<u64>(&g_rutas);
        svcCallSecureMonitor(&args);
        if (args.X[0] != 0) return Donde::NoSeSabe;
        const u32 magia = static_cast<u32>(args.X[1]);
        const u32 clase = static_cast<u32>(args.X[1] >> 32);
        if (magia != 0x30534645 || (clase != 1 && clase != 2)) return Donde::NoSeSabe;
        if (!std::memchr(g_rutas.nintendo, 0, sizeof(g_rutas.nintendo))) return Donde::NoSeSabe;

        ruta = g_rutas.nintendo;
        if (ruta.empty()) ruta = fmt::format("emummc/Nintendo_{:04x}", static_cast<u32>(args.X[2]));  // lo que usa Atmosphère por defecto
        if (!ruta.empty() && ruta[0] == '/') ruta.erase(0, 1);
        std::transform(ruta.begin(), ruta.end(), ruta.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        bool valida = ruta.starts_with("emummc/") && ruta.size() > 7 && !ruta.ends_with('/') && ruta.find("//") == std::string::npos &&
                      ruta.find('\\') == std::string::npos && ruta.find(':') == std::string::npos;
        for (size_t ini = 0; valida && ini < ruta.size();) {
            const size_t fin = ruta.find('/', ini);
            const std::string parte = ruta.substr(ini, fin - ini);
            if (parte == "." || parte == "..") valida = false;
            if (fin == std::string::npos) break;
            ini = fin + 1;
        }
        return valida ? Donde::Emunand : Donde::SinCarpeta;
    }
}

namespace limpiarEmunand {
    Plan armar()
    {
        Plan p;
        p.donde = carpetaNintendo(p.nintendo);
        if (p.donde != Donde::Emunand) return p;

        const std::set<u64> usb = homeRotos::juegosNexus();
        std::set<u64> nexus;

        // Juegos registrados en esta emuNAND.
        std::vector<NsApplicationRecord> pagina(128);
        std::set<u64> vistos;
        for (s32 desde = 0; desde < 65536;) {
            s32 leidos = 0;
            if (R_FAILED(nsListApplicationRecord(pagina.data(), static_cast<s32>(pagina.size()), desde, &leidos))) {
                p.error = true;
                return p;
            }
            if (leidos <= 0) break;
            for (s32 i = 0; i < leidos; ++i) {
                const u64 id = pagina[i].application_id;
                if (!esJuego(id) || !vistos.insert(id).second) continue;
                if (usb.count(id & MASCARA)) {
                    nexus.insert(id & MASCARA);
                    p.identidad.push_back("nexus:" + hex(id));
                    continue;
                }
                p.juegos.push_back(id);
                s32 n = 0;
                if (R_FAILED(nsCountApplicationContentMeta(id, &n))) n = 0;  // sin contenido: solo queda el registro
                std::vector<NsApplicationContentMetaStatus> metas(std::max<s32>(n, 0));
                s32 leidasMetas = 0;
                if (n > 0 && R_FAILED(nsListApplicationContentMetaStatus(id, 0, metas.data(), n, &leidasMetas))) leidasMetas = 0;
                std::string id_juego = "app:" + hex(id);
                for (s32 k = 0; k < leidasMetas; ++k) {
                    const auto& m = metas[k];
                    p.actualizaciones += m.meta_type == NcmContentMetaType_Patch || m.meta_type == NcmContentMetaType_DataPatch;
                    p.dlc += m.meta_type == NcmContentMetaType_AddOnContent;
                    id_juego += fmt::format(":{}-{}-{}-{}", hex(m.application_id), m.meta_type, m.storageID, m.version);
                }
                p.identidad.push_back(id_juego);
            }
            desde += leidos;
            if (leidos < static_cast<s32>(pagina.size())) break;
        }
        p.nexus = nexus.size();

        // Partidas de todos los usuarios, de juegos (no las del sistema).
        FsSaveDataInfoReader lector{};
        if (R_FAILED(fsOpenSaveDataInfoReader(&lector, FsSaveDataSpaceId_User))) {
            p.error = true;
            return p;
        }
        FsSaveDataInfo info[64];
        for (;;) {
            s64 n = 0;
            if (R_FAILED(fsSaveDataInfoReaderRead(&lector, info, 64, &n))) {
                p.error = true;
                break;
            }
            if (n <= 0) break;
            for (s64 i = 0; i < n; ++i) {
                const FsSaveDataInfo& s = info[i];
                if (s.save_data_type != FsSaveDataType_Account && s.save_data_type != FsSaveDataType_Device) continue;
                if (!esJuego(s.application_id) || s.system_save_data_id || !s.save_data_id) continue;
                if (s.save_data_space_id != FsSaveDataSpaceId_User) continue;
                if (nexus.count(s.application_id & MASCARA)) continue;  // las de los juegos del USB se quedan
                p.partidas.push_back(s);
                p.identidad.push_back(fmt::format("save:{}:{}:{}{}:{}:{}:{}", hex(s.save_data_id), hex(s.application_id), hex(s.uid.uid[0]),
                                                  hex(s.uid.uid[1]), s.save_data_type, s.save_data_rank, s.save_data_index));
            }
        }
        fsSaveDataInfoReaderClose(&lector);
        std::sort(p.identidad.begin(), p.identidad.end());

        // Que no haya cambiado el arranque mientras se revisaba.
        std::string otra;
        if (carpetaNintendo(otra) != Donde::Emunand || otra != p.nintendo) p.error = true;
        return p;
    }

    Resultado ejecutar(const Plan& plan, bool borrarPartidas)
    {
        Resultado r;
        std::ofstream log(LOG, std::ios::trunc);
        if (log) log << fmt::format("carpeta={} juegos={} partidas={} borrar_partidas={} nexus={}\n", plan.nintendo, plan.juegos.size(),
                                    plan.partidas.size(), borrarPartidas ? 1 : 0, plan.nexus);

        /* Se vuelve a revisar todo justo antes de borrar: si cambió algo desde
           la vista previa (otro juego, otra partida, otro arranque), no se
           borra nada. */
        const Plan ahora = armar();
        if (ahora.donde != Donde::Emunand || ahora.error || ahora.nintendo != plan.nintendo || ahora.identidad != plan.identidad) {
            r.cambio = true;
            if (log) log << "cambio desde la vista previa: no se borró nada\n";
            return r;
        }
        auto sigue = [&plan]() {
            std::string ruta;
            return carpetaNintendo(ruta) == Donde::Emunand && ruta == plan.nintendo;
        };

        // Primero las partidas, para que quitar un juego no las mueva antes.
        if (borrarPartidas) {
            for (const FsSaveDataInfo& s : plan.partidas) {
                if (!sigue()) {
                    r.cambio = true;
                    break;
                }
                actividad::detalle(fmt::format("{:016X}", s.application_id));
                FsSaveDataExtraData extra{};
                Result rc = fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&extra, sizeof(extra), FsSaveDataSpaceId_User, s.save_data_id);
                if (R_SUCCEEDED(rc)) {
                    const auto& a = extra.attr;
                    // Que sea exactamente la misma partida que se mostró.
                    if (a.application_id != s.application_id || a.system_save_data_id || a.save_data_type != s.save_data_type ||
                        a.uid.uid[0] != s.uid.uid[0] || a.uid.uid[1] != s.uid.uid[1] || a.save_data_rank != s.save_data_rank ||
                        a.save_data_index != s.save_data_index)
                        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
                    else
                        rc = fsDeleteSaveDataFileSystemBySaveDataSpaceId(FsSaveDataSpaceId_User, s.save_data_id);
                }
                R_SUCCEEDED(rc) ? ++r.partidas : ++r.partidasMal;
                if (log) log << fmt::format("partida {} juego {} {:X}\n", hex(s.save_data_id), hex(s.application_id), rc);
            }
        }

        for (const u64 id : plan.juegos) {
            if (r.cambio || !sigue()) {
                r.cambio = true;
                break;
            }
            actividad::detalle(hex(id));
            Result rc = nsDeleteApplicationCompletely(id);
            Result rc2 = 0;
            if (R_FAILED(rc)) {
                // Un registro copiado puede quedar aunque no haya contenido: entonces solo el registro.
                bool instalado = true;
                if (R_SUCCEEDED(nsIsAnyApplicationEntityInstalled(id, &instalado)) && !instalado) {
                    rc2 = homeRotos::quitarRegistro(id);
                    if (R_SUCCEEDED(rc2)) rc = 0;
                }
            }
            R_SUCCEEDED(rc) ? ++r.juegos : ++r.juegosMal;
            if (log) log << fmt::format("juego {} completo={:X} registro={:X}\n", hex(id), rc, rc2);
        }

        const Plan queda = armar();
        if (queda.donde == Donde::Emunand && !queda.error) {
            r.quedanJuegos = static_cast<int>(queda.juegos.size());
            r.quedanPartidas = static_cast<int>(queda.partidas.size());
        }
        if (log) log << fmt::format("quedan juegos={} partidas={}\n", r.quedanJuegos, r.quedanPartidas);
        return r;
    }
}
