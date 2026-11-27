#include "cuentas_offline.hpp"

#include <switch.h>

#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

#include <borealis.hpp>
#include "actividad.hpp"
#include "constants.hpp"

namespace cuentasOffline {

    namespace {

        constexpr u64 PARTIDA_CUENTAS = 0x8000000000000010;
        constexpr const char* MONTAJE = "cuentasoqb";

        struct Perfil {
            AccountUid uid;
            std::string uidTexto;  // como lo nombra la partida: baas/<uid>.dat
            u64 nasAnterior = 0;   // la vinculacion que tenia (para quitar sus archivos)
        };

        // Lo mismo que linkalho: el nombre del archivo en baas/ es el uid en este formato.
        std::string uidTexto(const AccountUid& uid)
        {
            std::stringstream s;
            s << std::setfill('0') << std::setw(8) << std::hex << (uid.uid[0] & 0xffffffff) << "-";
            s << std::setfill('0') << std::setw(4) << std::hex << ((uid.uid[0] >> 32) & 0xffff) << "-";
            s << std::setfill('0') << std::setw(4) << std::hex << ((uid.uid[0] >> 48) & 0xffff) << "-";
            s << std::setfill('0') << std::setw(2) << std::hex << (uid.uid[1] & 0xff);
            s << std::setfill('0') << std::setw(2) << std::hex << ((uid.uid[1] >> 8) & 0xff) << "-";
            s << std::setfill('0') << std::setw(8) << std::hex << ((uid.uid[1] >> 32) & 0xffffffff);
            s << std::setfill('0') << std::setw(4) << std::hex << ((uid.uid[1] >> 16) & 0xffff);
            return s.str();
        }

        Result baasAdministrador(Service* acc, const AccountUid uid, Service* out)
        {
            return serviceDispatchIn(acc, 250, uid, .out_num_objects = 1, .out_objects = out);
        }

        // Cada paso, para soporte: si no vincula, aqui se ve por que.
        std::ofstream& registro()
        {
            static std::ofstream f(std::string(CONFIG_PATH) + "cuentas_offline.log", std::ios::trunc);
            return f;
        }

        Result estaVinculado(Service* baas, bool* vinculado)
        {
            return serviceDispatchOut(baas, 250, *vinculado);
        }

        Result nasId(Service* baas, u64* id)
        {
            return serviceDispatchOut(baas, 120, *id);
        }

        std::mt19937_64 motor(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());

        u64 azar() { return std::uniform_int_distribution<u64>(0, ~0ULL)(motor); }

        std::string alfanumerico(size_t n)
        {
            static constexpr const char* letras = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
            std::uniform_int_distribution<size_t> d(0, std::strlen(letras) - 1);
            std::string out(n, '\0');
            for (auto& c : out) c = letras[d(motor)];
            return out;
        }

        std::string idioma()
        {
            u64 codigo = 0;
            if (R_SUCCEEDED(setInitialize())) {
                if (R_FAILED(setGetSystemLanguage(&codigo))) codigo = 0;
                setExit();
            }
            if (!codigo) return "en-US";
            char t[9] = {};
            std::memcpy(t, &codigo, 8);
            return t;
        }

        std::string zonaHoraria()
        {
            TimeLocationName tl{};
            if (R_SUCCEEDED(setsysInitialize())) {
                const Result rc = setsysGetDeviceTimeZoneLocationName(&tl);
                setsysExit();
                if (R_SUCCEEDED(rc) && tl.name[0]) return tl.name;
            }
            return "America/Guayaquil";
        }

        std::string reemplazar(std::string t, const std::string& de, const std::string& a)
        {
            const size_t p = t.find(de);
            if (p != std::string::npos) t.replace(p, de.size(), a);
            return t;
        }

        // Los tres archivos de una vinculación, como los escribe linkalho.
        bool escribirVinculacion(const std::string& su, const std::string& uid, const std::string& lengua, const std::string& zona)
        {
            const u64 nasId = azar(), baasUserId = azar(), cuentaId = azar();
            static const u64 CABECERA2 = 0x0000006E00000001, CABECERA3 = 0x0000000100000001;
            std::stringstream nas;
            nas << std::hex << nasId;
            const std::string nasTexto = nas.str();

            {
                std::ofstream f(su + "/baas/" + uid + ".dat", std::ios::binary | std::ios::trunc);
                f.write(reinterpret_cast<const char*>(&cuentaId), 8);
                f.write(reinterpret_cast<const char*>(&CABECERA2), 8);
                f.write(reinterpret_cast<const char*>(&nasId), 8);
                f.write(reinterpret_cast<const char*>(&CABECERA3), 8);
                f.write(reinterpret_cast<const char*>(&baasUserId), 8);
                f << alfanumerico(40);
                if (!f) return false;
            }
            {
                std::ofstream f(su + "/nas/" + nasTexto + ".dat", std::ios::binary | std::ios::trunc);
                f << alfanumerico(128);
                if (!f) return false;
            }
            {
                std::string perfil =
                    R"({"id":"#NAS_ID#","language":"#LOCALE#","timezone":"#TIMEZONE#","country":"#COUNTRY#","analyticsOptedIn":false,"gender":"male","emailOptedIn":false,"birthday":"1980-01-01","isChild":false,"email":"•","screenName":"•","region":"","loginId":"•","nickname":"•","isNnLinked":false,"isTwitterLinked":false,"isFacebookLinked":false,"isGoogleLinked":false})";
                perfil = reemplazar(perfil, "#NAS_ID#", nasTexto);
                perfil = reemplazar(perfil, "#LOCALE#", lengua);
                perfil = reemplazar(perfil, "#TIMEZONE#", zona);
                perfil = reemplazar(perfil, "#COUNTRY#", "EC");
                std::ofstream f(su + "/nas/" + nasTexto + "_user.json", std::ios::binary | std::ios::trunc);
                f << perfil;
                if (!f) return false;
            }
            return true;
        }

        std::string fecha()
        {
            const std::time_t t = std::time(nullptr);
            std::tm tm = *std::localtime(&t);
            std::stringstream s;
            s << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
            return s.str();
        }

    }  // namespace

    Resultado vincular()
    {
        Resultado r;

        // 1. Los perfiles y cuáles ya tienen cuenta (antes de detener el servicio de cuentas).
        std::vector<Perfil> aVincular;
        auto& log = registro();
        /* La sesion de administrador (acc:su) propia: la de libnx puede ser otra
           (acc:u0) si algo ya la abrio, y esa no responde por la vinculacion. */
        Service acc{};
        Result rc = smGetService(&acc, "acc:su");
        log << fmt::format("acc:su={:X}\n", rc) << std::flush;
        if (R_FAILED(rc)) {
            r.error = true;
            return r;
        }
        AccountUid uids[ACC_USER_LIST_SIZE] = {};
        s32 cuantos = 0;
        // IAccountServiceForAdministrator 2: ListAllUsers
        rc = serviceDispatch(&acc, 2, .buffer_attrs = {SfBufferAttr_HipcPointer | SfBufferAttr_Out}, .buffers = {{uids, sizeof(uids)}});
        if (R_SUCCEEDED(rc)) {
            cuantos = 0;
            for (const auto& u : uids)
                if (u.uid[0] || u.uid[1]) ++cuantos;
        }
        log << fmt::format("lista={:X} perfiles={}\n", rc, cuantos) << std::flush;
        if (R_SUCCEEDED(rc)) {
            for (s32 i = 0; i < cuantos; ++i) {
                Service baas;
                bool vinculado = false;
                /* Como linkalho: se vinculan todos y se sobrescribe lo que hubiera
                   (una vinculacion falsa de antes o una cuenta Nintendo real). Las
                   partidas no se tocan. */
                u64 nas = 0;
                Result rcB = baasAdministrador(&acc, uids[i], &baas), rcV = 0;
                if (R_SUCCEEDED(rcB)) {
                    rcV = estaVinculado(&baas, &vinculado);
                    if (R_SUCCEEDED(rcV) && vinculado && R_FAILED(nasId(&baas, &nas))) nas = 0;
                    serviceClose(&baas);
                }
                log << fmt::format("perfil {} baas={:X} consulta={:X} vinculado={} nas={:016x}\n", uidTexto(uids[i]), rcB, rcV, vinculado ? 1 : 0, nas) << std::flush;
                if (vinculado) ++r.yaTenian;
                aVincular.push_back({uids[i], uidTexto(uids[i]), nas});
            }
        }
        else
            r.error = true;
        serviceClose(&acc);
        log.flush();
        if (r.error || aVincular.empty()) return r;

        const std::string lengua = idioma(), zona = zonaHoraria();

        // 2. Detener cuentas, BCAT y OLSC para poder abrir su partida.
        actividad::paso("Deteniendo el servicio de cuentas");
        r.reiniciar = true;
        if (R_SUCCEEDED(pmshellInitialize())) {
            pmshellTerminateProgram(0x010000000000000C);  // BCAT
            pmshellTerminateProgram(0x010000000000001E);  // cuentas
            pmshellTerminateProgram(0x010000000000003E);  // OLSC
            pmshellExit();
        }

        FsFileSystem fs{};
        rc = fsOpen_SystemSaveData(&fs, FsSaveDataSpaceId_System, PARTIDA_CUENTAS, (AccountUid){0});
        log << fmt::format("partida={:X}\n", rc) << std::flush;
        if (R_FAILED(rc)) {
            r.error = true;
            return r;
        }
        if (fsdevMountDevice(MONTAJE, fs) < 0) {
            fsFsClose(&fs);
            r.error = true;
            return r;
        }
        const std::string su = std::string(MONTAJE) + ":/su";

        // 3. Respaldo de su/ en la SD antes de escribir nada.
        actividad::paso("Respaldando las cuentas");
        r.respaldo = std::string(CONFIG_PATH) + "cuentas_respaldo/" + fecha();
        std::error_code ec;
        std::filesystem::create_directories(r.respaldo, ec);
        /* Copia archivo por archivo: std::filesystem::copy se cortaba a la mitad
           con la partida montada (quedaban cache/ y nas/, sin baas/ ni registry). */
        bool copiaBien = !ec;
        int copiados = 0;
        for (auto it = std::filesystem::recursive_directory_iterator(su, ec); copiaBien && !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            const std::string origen = it->path().string();
            const std::string destino = r.respaldo + "/su" + origen.substr(su.size());
            std::error_code e2;
            if (it->is_directory(e2)) {
                std::filesystem::create_directories(destino, e2);
                continue;
            }
            std::ifstream in(origen, std::ios::binary);
            std::filesystem::create_directories(destino.substr(0, destino.rfind('/')), e2);
            std::ofstream out(destino, std::ios::binary | std::ios::trunc);
            out << in.rdbuf();
            out.close();
            if (!in || !out) {
                log << "no se copio " << origen << std::endl;
                copiaBien = false;
            }
            else
                ++copiados;
        }
        if (ec) copiaBien = false;
        log << "respaldo=" << r.respaldo << " archivos=" << copiados << " bien=" << copiaBien << " error=" << ec.message() << std::endl;
        if (!copiaBien) {
            // Sin respaldo no se escribe.
            fsdevUnmountDevice(MONTAJE);
            r.error = true;
            return r;
        }

        // 4. Las vinculaciones.
        std::filesystem::create_directories(su + "/baas", ec);
        std::filesystem::create_directories(su + "/nas", ec);
        for (const Perfil& p : aVincular) {
            actividad::paso("Vinculando " + p.uidTexto);
            // La vinculacion anterior fuera: su baas y sus archivos nas (con y sin ceros delante).
            std::filesystem::remove(su + "/baas/" + p.uidTexto + ".dat", ec);
            if (p.nasAnterior) {
                for (const std::string& n : {fmt::format("{:016x}", p.nasAnterior), fmt::format("{:x}", p.nasAnterior)}) {
                    std::filesystem::remove(su + "/nas/" + n + ".dat", ec);
                    std::filesystem::remove(su + "/nas/" + n + "_user.json", ec);
                }
            }
            escribirVinculacion(su, p.uidTexto, lengua, zona) ? ++r.vinculadas : ++r.fallos;
        }

        log << fmt::format("vinculadas={} fallos={}\n", r.vinculadas, r.fallos) << std::flush;
        if (R_FAILED(fsdevCommitDevice(MONTAJE))) {
            r.fallos += r.vinculadas;
            r.vinculadas = 0;
        }
        fsdevUnmountDevice(MONTAJE);
        return r;
    }
}
