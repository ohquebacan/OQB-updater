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

    std::set<u64> registros()
    {
        std::set<u64> ids;
        constexpr s32 PAGINA = 128;
        std::vector<NsApplicationRecord> pagina(PAGINA);
        for (s32 desde = 0; desde < 65536; desde += PAGINA) {
            s32 leidos = 0;
            if (R_FAILED(nsListApplicationRecord(pagina.data(), PAGINA, desde, &leidos)) || leidos <= 0)
                break;
            for (s32 i = 0; i < leidos; ++i)
                if (pagina[i].application_id) ids.insert(pagina[i].application_id);
            if (leidos < PAGINA)
                break;
        }
        return ids;
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

    bool seLee(u64 id, NsApplicationControlSource fuente, NsApplicationControlData* control)
    {
        u64 size = 0;
        return R_SUCCEEDED(nsGetApplicationControlData(fuente, id, control, sizeof(NsApplicationControlData), &size)) &&
               size >= sizeof(control->nacp);
    }

    bool deCartucho(u64 id)
    {
        NsApplicationContentMetaStatus estados[16] = {};
        s32 n = 0;
        if (R_FAILED(nsListApplicationContentMetaStatus(id, 0, estados, 16, &n)))
            return false;
        for (s32 i = 0; i < n; ++i)
            if (estados[i].storageID == NcmStorageId_GameCard)
                return true;
        return false;
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
        for (const u64 id : registros()) {
            // Primero la caché: es lo que usa el HOME y no lee el juego, así
            // que con una biblioteca grande esto sigue siendo rápido.
            if (seLee(id, NsApplicationControlSource_CacheOnly, control.get()))
                continue;
            if (seLee(id, NsApplicationControlSource_Storage, control.get()))
                continue;
            if (usb.count(id & MASCARA) || deCartucho(id))
                continue;
            rotos.push_back({id});
        }
        return rotos;
    }

    int quitar(const std::vector<Entrada>& lista)
    {
        for (const Entrada& e : lista) {
            actividad::detalle(fmt::format("{:016X}", e.id));
            /* Completo quita también lo que quede del contenido (NCA sueltos,
               la entrada en la base de datos). Si falla, al menos el registro,
               que es lo que dibuja el ícono. Las partidas no se tocan. */
            if (R_FAILED(nsDeleteApplicationCompletely(e.id)))
                borrarRegistro(e.id);
        }
        const std::set<u64> quedan = registros();
        int quitados = 0;
        for (const Entrada& e : lista)
            if (!quedan.count(e.id)) ++quitados;
        return quitados;
    }

    std::string listado(const std::vector<Entrada>& lista, size_t maximo)
    {
        std::string s;
        for (size_t i = 0; i < lista.size() && i < maximo; ++i)
            s += fmt::format("{:016X}\n", lista[i].id);
        if (lista.size() > maximo)
            s += fmt::format("menus/tools/home_rotos_mas"_i18n, lista.size() - maximo) + "\n";
        return s;
    }
}
