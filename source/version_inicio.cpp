#include "version_inicio.hpp"

#include <borealis.hpp>

#include <algorithm>
#include <fstream>
#include <memory>
#include <set>

#include "actividad.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {
    constexpr const char LOG[] = "/config/aio-switch-updater/version_inicio.log";

    bool esJuego(u64 id)
    {
        return (id >= 0x0100000000010000ULL && id < 0x0200000000000000ULL) || (id >> 56) == 0x05;
    }

    // IApplicationManagerInterface 36: PushLaunchVersion. Mismo formato que libnx-ext.
    Result nsPonerVersion(u64 id, u32 version)
    {
        const struct {
            u32 version;
            u64 app_id;
            u8 pad[4];
        } in = {version, id, {}};
        if (hosversionBefore(3, 0, 0))
            return serviceDispatchIn(nsGetServiceSession_ApplicationManagerInterface(), 36, in);
        Service am;
        Result rc = nsGetApplicationManagerInterface(&am);
        if (R_FAILED(rc))
            return rc;
        rc = serviceDispatchIn(&am, 36, in);
        serviceClose(&am);
        return rc;
    }

    // Versión más alta instalada del juego o su actualización (los DLC numeran aparte).
    bool versionInstalada(u64 id, u32& version)
    {
        NsApplicationContentMetaStatus metas[32] = {};
        s32 n = 0;
        if (R_FAILED(nsListApplicationContentMetaStatus(id, 0, metas, 32, &n)) || n <= 0)
            return false;
        bool hay = false;
        version = 0;
        for (s32 i = 0; i < n; ++i) {
            if (metas[i].meta_type != NcmContentMetaType_Application && metas[i].meta_type != NcmContentMetaType_Patch) continue;
            if (metas[i].storageID == NcmStorageId_None) continue;
            version = std::max(version, metas[i].version);
            hay = true;
        }
        return hay;
    }

    std::string nombreDe(u64 id)
    {
        auto control = std::make_unique<NsApplicationControlData>();
        u64 tam = 0;
        if (R_FAILED(nsGetApplicationControlData(NsApplicationControlSource_CacheOnly, id, control.get(), sizeof(NsApplicationControlData), &tam)) ||
            tam < sizeof(control->nacp))
            return fmt::format("{:016X}", id);
        NacpLanguageEntry* entrada = nullptr;
        if (R_SUCCEEDED(nacpGetLanguageEntry(&control->nacp, &entrada)) && entrada && entrada->name[0])
            return entrada->name;
        return fmt::format("{:016X}", id);
    }
}

namespace versionInicio {
    Result poner(u64 id, u32 version)
    {
        Result primero = 0;
        if (hosversionAtLeast(6, 0, 0)) {
            Result rc = avmInitialize();
            if (R_SUCCEEDED(rc)) {
                rc = avmPushLaunchVersion(id, version);
                if (R_FAILED(rc) && R_SUCCEEDED(primero)) primero = rc;
                rc = avmUpgradeLaunchRequiredVersion(id, version);
                if (R_FAILED(rc) && R_SUCCEEDED(primero)) primero = rc;
                avmExit();
            } else {
                primero = rc;
            }
        }
        const Result rc = nsPonerVersion(id, version);
        if (R_FAILED(rc) && R_SUCCEEDED(primero)) primero = rc;
        return primero;
    }

    std::vector<Pendiente> buscar()
    {
        std::vector<Pendiente> lista;
        if (hosversionBefore(6, 0, 0) || R_FAILED(avmInitialize()))
            return lista;
        std::ofstream log(LOG, std::ios::trunc);
        std::vector<NsApplicationRecord> pagina(128);
        std::set<u64> vistos;
        for (s32 desde = 0; desde < 65536;) {
            s32 leidos = 0;
            if (R_FAILED(nsListApplicationRecord(pagina.data(), static_cast<s32>(pagina.size()), desde, &leidos)) || leidos <= 0) break;
            for (s32 i = 0; i < leidos; ++i) {
                const u64 id = pagina[i].application_id;
                if (!esJuego(id) || !vistos.insert(id).second) continue;
                u32 instalada = 0, pide = 0;
                if (!versionInstalada(id, instalada)) continue;  // sin contenido instalado: nada que arreglar todavía
                if (R_FAILED(avmGetLaunchRequiredVersion(id, &pide))) continue;
                if (log) log << fmt::format("{:016X} pide={} instalada={}\n", id, pide, instalada);
                if (pide > instalada) lista.push_back({id, pide, instalada, nombreDe(id)});
            }
            desde += leidos;
            if (leidos < static_cast<s32>(pagina.size())) break;
        }
        avmExit();
        return lista;
    }

    int arreglar(const std::vector<Pendiente>& lista)
    {
        std::ofstream log(LOG, std::ios::app);
        int bien = 0;
        for (const Pendiente& p : lista) {
            actividad::detalle(p.nombre);
            const Result rc = poner(p.id, p.instalada);
            u32 ahora = p.pide;
            if (R_SUCCEEDED(avmInitialize())) {
                if (R_FAILED(avmGetLaunchRequiredVersion(p.id, &ahora))) ahora = p.pide;
                avmExit();
            }
            if (ahora <= p.instalada) ++bien;
            if (log) log << fmt::format("arreglar {:016X} a {} rc={:X} ahora={}\n", p.id, p.instalada, rc, ahora);
        }
        return bien;
    }

    std::string listado(const std::vector<Pendiente>& lista, size_t maximo)
    {
        std::string s;
        for (size_t i = 0; i < lista.size() && i < maximo; ++i)
            s += fmt::format("menus/tools/version_inicio_linea"_i18n, lista[i].nombre, lista[i].pide, lista[i].instalada) + "\n";
        if (lista.size() > maximo)
            s += fmt::format("menus/tools/home_rotos_mas"_i18n, lista.size() - maximo) + "\n";
        return s;
    }
}
