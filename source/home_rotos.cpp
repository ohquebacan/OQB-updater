#include "home_rotos.hpp"

#include <borealis.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>

#include "actividad.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {
    constexpr const char TITLE_MAP[] = "/atmosphere/usb_content/title_map.bin";
    constexpr const char USB_BADGES[] = "/atmosphere/usb_badges";
    constexpr u64 MASCARA = ~0x1FFFULL;  // juego, actualización y DLC como uno

    constexpr const char LOG[] = "/config/aio-switch-updater/home_rotos.log";

    std::vector<NsApplicationRecord> registrosCompletos()
    {
        std::vector<NsApplicationRecord> todos;
        constexpr s32 PAGINA = 128;
        std::vector<NsApplicationRecord> pagina(PAGINA);
        for (s32 desde = 0; desde < 65536; desde += PAGINA) {
            s32 leidos = 0;
            if (R_FAILED(nsListApplicationRecord(pagina.data(), PAGINA, desde, &leidos)) || leidos <= 0)
                break;
            for (s32 i = 0; i < leidos; ++i)
                if (pagina[i].application_id) todos.push_back(pagina[i]);
            if (leidos < PAGINA)
                break;
        }
        return todos;
    }

    std::set<u64> registros()
    {
        std::set<u64> ids;
        for (const NsApplicationRecord& r : registrosCompletos())
            ids.insert(r.application_id);
        return ids;
    }

    // "app:sd upd:sd" (tipo:almacenamiento) de lo que el registro dice tener.
    std::string contenido(u64 id)
    {
        NsApplicationContentMetaStatus estados[16] = {};
        s32 n = 0;
        const Result rc = nsListApplicationContentMetaStatus(id, 0, estados, 16, &n);
        if (R_FAILED(rc))
            return fmt::format("error {:X}", rc);
        std::string s;
        for (s32 i = 0; i < n; ++i) {
            const u8 t = estados[i].meta_type;
            const char* tipo = t == NcmContentMetaType_Application ? "app" : t == NcmContentMetaType_Patch ? "upd" : t == NcmContentMetaType_AddOnContent ? "dlc" : "otro";
            const u8 st = estados[i].storageID;
            const char* donde = st == NcmStorageId_GameCard ? "gc" : st == NcmStorageId_SdCard ? "sd" : st == NcmStorageId_BuiltInUser ? "nand" : st == NcmStorageId_None ? "ninguno" : "otro";
            if (!s.empty()) s += ' ';
            s += fmt::format("{}:{}", tipo, donde);
        }
        return s;
    }

    /* Los juegos que Nexus tiene en el USB (su tabla de rutas y las marcas
       del HOME), igual que en la sección de Nexus. Sin el USB conectado no se
       pueden leer y parecerían dañados. */
    std::set<u64> juegosEnUsb()
    {
        std::set<u64> juegos;
        std::ifstream f(TITLE_MAP, std::ios::binary);
        if (f) {
            std::stringstream s;
            s << f.rdbuf();
            const std::string b = s.str();
            auto u32At = [&b](size_t p) { u32 v; std::memcpy(&v, b.data() + p, 4); return v; };
            auto u64At = [&b](size_t p) { u64 v; std::memcpy(&v, b.data() + p, 8); return v; };
            if (b.size() >= 16 && u32At(0) == 0x54425355 && u32At(4) == 1) {
                const u32 n = u32At(8);
                if (b.size() == 16 + size_t(n) * 16)
                    for (u32 i = 0; i < n; ++i) {
                        const u64 id = u64At(16 + size_t(i) * 16);
                        if (id) juegos.insert(id & MASCARA);
                    }
            }
        }
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(USB_BADGES, ec)) {
            const std::string n = e.path().filename().string();
            if (n.size() != 22 || n.compare(16, 6, "-0.ubg") != 0) continue;
            char* fin = nullptr;
            const u64 id = std::strtoull(n.substr(0, 16).c_str(), &fin, 16);
            if (id && fin && *fin == 0) juegos.insert(id & MASCARA);
        }
        return juegos;
    }

    // IApplicationManagerInterface 27: DeleteApplicationRecord (como Sphaira).
    Result borrarRegistro(u64 id)
    {
        if (hosversionBefore(3, 0, 0))
            return serviceDispatchIn(nsGetServiceSession_ApplicationManagerInterface(), 27, id);
        Service am;
        Result rc = nsGetApplicationManagerInterface(&am);
        if (R_FAILED(rc))
            return rc;
        rc = serviceDispatchIn(&am, 27, id);
        serviceClose(&am);
        return rc;
    }
}

namespace homeRotos {
    std::vector<Entrada> buscar()
    {
        std::vector<Entrada> rotos;
        const std::set<u64> usb = juegosEnUsb();
        auto control = std::make_unique<NsApplicationControlData>();
        /* Una línea por juego con lo que se miró, para soporte: si un ícono
           cargando no aparece en la lista, aquí se ve por qué. */
        std::ofstream log(LOG, std::ios::trunc);
        for (const NsApplicationRecord& r : registrosCompletos()) {
            const u64 id = r.application_id;
            u64 tam = 0;
            // Primero la caché: es lo que usa el HOME y no lee el juego, así
            // que con una biblioteca grande esto sigue siendo rápido.
            const Result rcCache = nsGetApplicationControlData(NsApplicationControlSource_CacheOnly, id, control.get(), sizeof(NsApplicationControlData), &tam);
            const bool cache = R_SUCCEEDED(rcCache) && tam >= sizeof(control->nacp);
            Result rcJuego = 0;
            bool juego = false;
            if (!cache) {
                rcJuego = nsGetApplicationControlData(NsApplicationControlSource_Storage, id, control.get(), sizeof(NsApplicationControlData), &tam);
                juego = R_SUCCEEDED(rcJuego) && tam >= sizeof(control->nacp);
            }
            std::string nombre;
            NacpLanguageEntry* entrada = nullptr;
            if ((cache || juego) && R_SUCCEEDED(nacpGetLanguageEntry(&control->nacp, &entrada)) && entrada)
                nombre = entrada->name;
            const bool enUsb = usb.count(id & MASCARA) > 0;
            const std::string metas = contenido(id);
            const bool cartucho = metas.find("gc") != std::string::npos;
            /* Los del USB de Nexus que funcionan tienen su ícono en la caché aunque
               el USB no esté. Sin caché ni contenido son entradas dañadas que dejó
               Nexus: también cuentan, pero solo se les quita el registro. */
            const bool roto = !cache && !juego && !cartucho;
            if (log)
                log << fmt::format("{:016X} evento={} cache={:X} juego={:X} usb={} contenido=[{}] {} {}\n", id, r.last_event,
                                   rcCache, rcJuego, enUsb ? 1 : 0, metas, roto ? "ROTO" : "ok", nombre);
            if (roto)
                rotos.push_back({id, enUsb});
        }
        return rotos;
    }

    int quitar(const std::vector<Entrada>& lista)
    {
        std::ofstream log(LOG, std::ios::app);
        for (const Entrada& e : lista) {
            actividad::detalle(fmt::format("{:016X}", e.id));
            /* Completo quita también lo que quede del contenido (NCA sueltos,
               la entrada en la base de datos). Si falla, al menos el registro,
               que es lo que dibuja el ícono. Las partidas no se tocan. */
            /* Si Nexus lo tiene en su tabla, solo el registro: así nunca se
               borra nada del USB. */
            const Result rc = e.nexus ? MAKERESULT(Module_Libnx, LibnxError_NotFound) : nsDeleteApplicationCompletely(e.id);
            Result rc2 = 0;
            if (R_FAILED(rc))
                rc2 = borrarRegistro(e.id);
            if (log)
                log << fmt::format("quitar {:016X} completo={:X} registro={:X}\n", e.id, rc, rc2);
        }
        const std::set<u64> quedan = registros();
        int quitados = 0;
        for (const Entrada& e : lista)
            if (!quedan.count(e.id)) ++quitados;
        if (log)
            log << fmt::format("quitados {} de {}\n", quitados, lista.size());
        return quitados;
    }

    std::set<u64> juegosNexus() { return juegosEnUsb(); }

    Result quitarRegistro(u64 id) { return borrarRegistro(id); }

    std::string listado(const std::vector<Entrada>& lista, size_t maximo)
    {
        // Dos por línea, para que el texto no baje hasta el botón.
        std::string s;
        for (size_t i = 0; i < lista.size() && i < maximo; ++i)
            s += fmt::format("{:016X}{}", lista[i].id, i + 1 == lista.size() || i + 1 == maximo ? "\n" : (i % 2 ? "\n" : "   "));
        if (lista.size() > maximo)
            s += fmt::format("menus/tools/home_rotos_mas"_i18n, lista.size() - maximo) + "\n";
        return s;
    }
}
