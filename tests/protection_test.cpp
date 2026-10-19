// Pruebas de protection::run(). Compilan el codigo real, no una copia: la
// funcion acepta una raiz, asi que se le arma una SD de mentira en /tmp.
//
//   c++ -std=c++17 -Iinclude -o /tmp/pt tests/protection_test.cpp source/protection.cpp && /tmp/pt

#include "protection.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

    int failures = 0;

    const std::string HOSTS_BLOCK =
        "# bloqueo OQB\n"
        "127.0.0.1 *nintendo.*\n"
        "127.0.0.1 *.nintendo.net\n";

    const std::string HOSTS_NO_BLOCK =
        "# sin reglas utiles\n"
        "127.0.0.1 ejemplo.local\n";

    void write(const std::string& path, const std::string& contents)
    {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream(path, std::ios::binary) << contents;
    }

    // Una SD con todas las protecciones puestas y solo default.txt, que es como
    // viene el pack OQB.
    std::string makeSd()
    {
        static int n = 0;
        const std::string root = (std::filesystem::temp_directory_path() /
                                  ("oqb-prot-" + std::to_string(++n)))
                                     .string();
        std::filesystem::remove_all(root);
        write(root + "/atmosphere/config/system_settings.ini",
              "[atmosphere]\nenable_dns_mitm = u8!0x1\n");
        write(root + "/exosphere.ini",
              "[exosphere]\nblank_prodinfo_emummc=1\nblank_prodinfo_sysmmc=1\n");
        write(root + "/atmosphere/hosts/default.txt", HOSTS_BLOCK);
        std::filesystem::create_directories(root + "/emuMMC");
        return root;
    }

    const protection::Check& find(const std::vector<protection::Check>& checks, const std::string& name)
    {
        for (const auto& c : checks)
            if (c.name == name) return c;
        std::cout << "FALLA  no se reporto ninguna comprobacion llamada \"" << name << "\"\n";
        ++failures;
        static protection::Check missing{"", protection::Status::Fail, ""};
        return missing;
    }

    const char* statusName(protection::Status s)
    {
        switch (s) {
            case protection::Status::Ok:
                return "Ok";
            case protection::Status::Warning:
                return "Warning";
            case protection::Status::Fail:
                return "Fail";
        }
        return "?";
    }

    void expect(const std::string& what, const std::vector<protection::Check>& checks,
                const std::string& name, protection::Status want)
    {
        const protection::Check& c = find(checks, name);
        if (c.status == want) {
            std::cout << "ok     " << what << "\n";
        }
        else {
            std::cout << "FALLA  " << what << "\n"
                      << "       " << name << ": esperaba " << statusName(want)
                      << ", salio " << statusName(c.status)
                      << " (\"" << c.detail << "\")\n";
            ++failures;
        }
    }

}  // namespace

int main()
{
    // El caso que daba la falsa alarma: el pack OQB trae solo default.txt y
    // esta protegido, porque Atmosphere cae a ese fichero cuando no encuentra
    // el del arranque. Antes esto reportaba dos fallas en rojo.
    {
        const std::string sd = makeSd();
        const auto checks = protection::run(sd);
        expect("default.txt que bloquea sale en verde", checks, "hosts/default.txt", protection::Status::Ok);
        expect("emummc.txt que falta NO es una falla", checks, "hosts/emummc.txt", protection::Status::Ok);
        expect("sysmmc.txt que falta NO es una falla", checks, "hosts/sysmmc.txt", protection::Status::Ok);
        expect("DNS MITM activado sale en verde", checks, "DNS MITM activado", protection::Status::Ok);
        if (protection::anyFailed(checks)) {
            std::cout << "FALLA  una SD correcta no deberia reportar ninguna falla\n";
            ++failures;
        }
        else {
            std::cout << "ok     una SD correcta no reporta ninguna falla\n";
        }
    }

    // El caso peligroso de verdad: el fichero del arranque existe, asi que deja
    // fuera a default.txt, y no bloquea. Eso si tiene que salir en rojo.
    {
        const std::string sd = makeSd();
        write(sd + "/atmosphere/hosts/emummc.txt", HOSTS_NO_BLOCK);
        const auto checks = protection::run(sd);
        expect("emummc.txt que existe y no bloquea ES una falla", checks, "hosts/emummc.txt", protection::Status::Fail);
        expect("default.txt sigue en verde", checks, "hosts/default.txt", protection::Status::Ok);
    }

    // Un fichero de arranque vacio tampoco bloquea, aunque exista.
    {
        const std::string sd = makeSd();
        write(sd + "/atmosphere/hosts/sysmmc.txt", "");
        const auto checks = protection::run(sd);
        expect("sysmmc.txt vacio ES una falla", checks, "hosts/sysmmc.txt", protection::Status::Fail);
    }

    // Si default.txt no bloquea y no hay fichero de arranque, no hay respaldo
    // ninguno: entonces si es una falla que falten.
    {
        const std::string sd = makeSd();
        write(sd + "/atmosphere/hosts/default.txt", HOSTS_NO_BLOCK);
        const auto checks = protection::run(sd);
        expect("default.txt que no bloquea ES una falla", checks, "hosts/default.txt", protection::Status::Fail);
        expect("sin respaldo, emummc.txt que falta ES una falla", checks, "hosts/emummc.txt", protection::Status::Fail);
        expect("sin respaldo, sysmmc.txt que falta ES una falla", checks, "hosts/sysmmc.txt", protection::Status::Fail);
    }

    // Un emummc.txt propio que si bloquea vale igual que default.txt.
    {
        const std::string sd = makeSd();
        write(sd + "/atmosphere/hosts/default.txt", HOSTS_NO_BLOCK);
        write(sd + "/atmosphere/hosts/emummc.txt", HOSTS_BLOCK);
        const auto checks = protection::run(sd);
        expect("emummc.txt propio que bloquea sale en verde", checks, "hosts/emummc.txt", protection::Status::Ok);
    }

    // DNS MITM: que la clave no aparezca es una falla, porque se depende del
    // valor por defecto de Atmosphere.
    {
        const std::string sd = makeSd();
        write(sd + "/atmosphere/config/system_settings.ini", "[atmosphere]\n");
        const auto checks = protection::run(sd);
        expect("enable_dns_mitm ausente ES una falla", checks, "DNS MITM activado", protection::Status::Fail);
    }
    {
        const std::string sd = makeSd();
        write(sd + "/atmosphere/config/system_settings.ini", "[atmosphere]\nenable_dns_mitm = u8!0x0\n");
        const auto checks = protection::run(sd);
        expect("enable_dns_mitm en 0 ES una falla", checks, "DNS MITM activado", protection::Status::Fail);
    }

    /* La prueba de DNS en la consola manda sobre el ajuste. Es el caso que
       reporto Emilio: Prelude dejo enable_dns_mitm en 0 al volver de Nextendo,
       pero el Atmosphere del pack ignora el ajuste y bloquea igual. */
    using Rt = protection::Runtime;
    {
        const std::string sd = makeSd();
        write(sd + "/atmosphere/config/system_settings.ini", "[atmosphere]\nenable_dns_mitm = u8!0x0\n");
        Rt rt;
        rt.dns = Rt::Dns::Blocked;
        rt.dnsDetail = "accounts.nintendo.com → 127.0.0.1";
        const auto checks = protection::run(sd, rt);
        expect("ajuste en 0 pero DNS bloqueado de verdad sale en verde", checks, "DNS MITM activado",
               protection::Status::Ok);
        if (protection::dnsBlockingOk(checks))
            std::cout << "ok     ajuste en 0 con bloqueo probado no frena la hora del sistema\n";
        else {
            std::cout << "FALLA  ajuste en 0 con bloqueo probado no deberia frenar la hora del sistema\n";
            ++failures;
        }
    }
    {
        // Modo Nextendo: el ajuste en 1, pero los hosts mandan a otro servidor.
        const std::string sd = makeSd();
        Rt rt;
        rt.dns = Rt::Dns::Redirected;
        rt.dnsDetail = "accounts.nintendo.com → 203.0.113.7";
        const auto checks = protection::run(sd, rt);
        expect("DNS que sale a internet ES una falla aunque el ajuste diga 1", checks, "DNS MITM activado",
               protection::Status::Fail);
    }
    {
        // Sin respuesta queda el ajuste, como antes.
        const std::string sd = makeSd();
        write(sd + "/atmosphere/config/system_settings.ini", "[atmosphere]\nenable_dns_mitm = u8!0x0\n");
        Rt rt;
        rt.dns = Rt::Dns::Unresolved;
        const auto checks = protection::run(sd, rt);
        expect("sin respuesta del DNS, el ajuste en 0 sigue siendo falla", checks, "DNS MITM activado",
               protection::Status::Fail);
    }

    // Version de Atmosphere contra firmware.
    {
        const std::string sd = makeSd();
        Rt rt;
        rt.firmware = (23 << 16) | (0 << 8) | 1;
        rt.atmosphere = (1 << 16) | (12 << 8) | 1;
        expect("Atmosphere 1.12.1 en 23.0.1 sale en verde", protection::run(sd, rt),
               "Bloqueo DNS en este firmware", protection::Status::Ok);
        rt.atmosphere = (1 << 16) | (12 << 8) | 0;
        expect("Atmosphere 1.12.0 en 23.0.1 es aviso (no se sabe si trae el arreglo)",
               protection::run(sd, rt), "Bloqueo DNS en este firmware", protection::Status::Warning);
        rt.atmosphere = (1 << 16) | (11 << 8) | 1;
        expect("Atmosphere 1.11.1 en 23.0.1 ES una falla", protection::run(sd, rt),
               "Bloqueo DNS en este firmware", protection::Status::Fail);

        rt.firmware = (22 << 16) | (5 << 8) | 0;
        const auto checks = protection::run(sd, rt);
        const bool present = std::any_of(checks.begin(), checks.end(), [](const protection::Check& c) {
            return c.name == "Bloqueo DNS en este firmware";
        });
        if (!present)
            std::cout << "ok     en firmware 22.5 no se compara la version\n";
        else {
            std::cout << "FALLA  en firmware 22.5 no deberia compararse la version\n";
            ++failures;
        }
    }

    /* Nextendo: Prelude cambia los hosts para mandar lo de Nintendo a sus
       servidores. Tiene que salir como falla (la consola si sale), pero
       explicada, y el version check no es cosa suya. */
    {
        const std::string sd = makeSd();
        write(sd + "/atmosphere/hosts/default.txt",
              "51.178.29.194 *.nintendo.com\n51.178.29.194 *.nintendo.net\n");
        write(sd + "/atmosphere/hosts/emummc.txt",
              "51.178.29.194 *.nintendo.com\n164.132.111.120 g2*.s.n.srv.nintendo.net\n");
        write(sd + "/exosphere.ini", "[exosphere]\nblank_prodinfo_emummc=0\nblank_prodinfo_sysmmc=1\n");
        write(sd + "/atmosphere/config/system_settings.ini", "[atmosphere]\nenable_dns_mitm = u8!0x0\n");
        Rt rt;
        rt.dns = Rt::Dns::Redirected;
        rt.dnsAddress = "51.178.29.194";
        rt.dnsDetail = "accounts.nintendo.com → 51.178.29.194";
        const auto checks = protection::run(sd, rt);
        expect("Nextendo completo sale en verde en su propia comprobacion", checks, protection::NEXTENDO_CHECK,
               protection::Status::Ok);
        expect("hosts de Nextendo ES una falla", checks, "hosts/default.txt", protection::Status::Fail);
        const bool all = std::all_of(checks.begin(), checks.end(), [](const protection::Check& c) {
            return c.status != protection::Status::Fail || c.byNextendo;
        });
        if (all && protection::failedOnlyByNextendo(checks) && !protection::dnsBlockingOk(checks))
            std::cout << "ok     con Nextendo las fallas se explican, pero el bloqueo sigue dandose por caido\n";
        else {
            std::cout << "FALLA  con Nextendo las fallas deberian marcarse y seguir contando\n";
            ++failures;
        }

        rt.firmware = (23 << 16) | 1;
        rt.atmosphere = (1 << 16) | (11 << 8);
        const auto with_old = protection::run(sd, rt);
        if (!protection::failedOnlyByNextendo(with_old))
            std::cout << "ok     un Atmosphere viejo no se le atribuye a Nextendo\n";
        else {
            std::cout << "FALLA  un Atmosphere viejo no deberia atribuirse a Nextendo\n";
            ++failures;
        }
    }
    {
        // Nextendo a medias: los hosts van a Nextendo, pero el numero de serie
        // sigue oculto. Es aviso, y lo demas se sigue explicando por Nextendo.
        const std::string sd = makeSd();
        write(sd + "/atmosphere/hosts/emummc.txt", "3.135.232.168 *.nintendo.com\n");
        const auto checks = protection::run(sd);
        expect("Nextendo con el numero de serie oculto es aviso", checks, protection::NEXTENDO_CHECK,
               protection::Status::Warning);
        if (protection::nextendoIncomplete(checks) && protection::failedOnlyByNextendo(checks))
            std::cout << "ok     Nextendo a medias se reporta como tal\n";
        else {
            std::cout << "FALLA  Nextendo a medias deberia reportarse\n";
            ++failures;
        }
    }
    {
        // Un servidor que no es de Nextendo (puede ser el de Nintendo): falla a
        // secas, nunca "por Nextendo", aunque haya hosts de Nextendo tambien.
        const std::string sd = makeSd();
        write(sd + "/atmosphere/hosts/emummc.txt", "51.178.29.194 *.nintendo.com\n");
        Rt rt;
        rt.dns = Rt::Dns::Redirected;
        rt.dnsAddress = "23.45.67.89";
        rt.dnsDetail = "accounts.nintendo.com → 23.45.67.89";
        const auto checks = protection::run(sd, rt);
        if (find(checks, "DNS MITM activado").status == protection::Status::Fail &&
            !find(checks, "DNS MITM activado").byNextendo && !protection::failedOnlyByNextendo(checks))
            std::cout << "ok     el DNS que sale a un servidor que no es Nextendo es falla a secas\n";
        else {
            std::cout << "FALLA  el DNS que sale a otro servidor no deberia atribuirse a Nextendo\n";
            ++failures;
        }
    }
    {
        const std::string sd = makeSd();
        write(sd + "/atmosphere/hosts/default.txt", "23.45.67.89 *.nintendo.com\n");
        const auto checks = protection::run(sd);
        if (!find(checks, "hosts/default.txt").byNextendo && !protection::failedOnlyByNextendo(checks))
            std::cout << "ok     hosts a un servidor desconocido no es Nextendo\n";
        else {
            std::cout << "FALLA  hosts a un servidor desconocido no deberia ser Nextendo\n";
            ++failures;
        }
    }

    {
        // El pack OQB manda ctest y conntest a 90DNS: eso no es Nextendo.
        const std::string sd = makeSd();
        write(sd + "/atmosphere/hosts/default.txt",
              HOSTS_BLOCK + "95.216.149.205 conntest.nintendowifi.net\n95.216.149.205 ctest.cdn.nintendo.net\n");
        const auto checks = protection::run(sd);
        expect("la prueba de conexion a 90DNS no cuenta como redireccion", checks, "hosts/default.txt",
               protection::Status::Ok);
    }
    {
        // Un hosts que no bloquea, sin Nextendo, sigue siendo falla a secas.
        const std::string sd = makeSd();
        write(sd + "/atmosphere/hosts/default.txt", HOSTS_NO_BLOCK);
        if (!protection::failedOnlyByNextendo(protection::run(sd)))
            std::cout << "ok     sin redireccion, las fallas no se atribuyen a Nextendo\n";
        else {
            std::cout << "FALLA  sin redireccion no deberia atribuirse a Nextendo\n";
            ++failures;
        }
    }

    {
        // Modo captura de Prelude: falla propia, nunca "por Nextendo".
        const std::string sd = makeSd();
        write(sd + "/switch/prelude_capture_mode.flag", "capture");
        write(sd + "/atmosphere/hosts/emummc.txt", "51.178.29.194 *.nintendo.com\n");
        const auto checks = protection::run(sd);
        expect("modo captura de Prelude ES una falla", checks, "Modo captura de Prelude", protection::Status::Fail);
        if (!find(checks, "Modo captura de Prelude").byNextendo && !protection::failedOnlyByNextendo(checks) &&
            !protection::dnsBlockingOk(checks))
            std::cout << "ok     el modo captura no se explica como Nextendo y frena la hora del sistema\n";
        else {
            std::cout << "FALLA  el modo captura no deberia explicarse como Nextendo\n";
            ++failures;
        }
    }

    {
        // Los parches de CA sueltos (de otros usos) no son el modo captura.
        const std::string sd = makeSd();
        std::filesystem::create_directories(sd + "/atmosphere/exefs_patches/disable_ca_verification");
        const auto checks = protection::run(sd);
        const bool present = std::any_of(checks.begin(), checks.end(), [](const protection::Check& c) {
            return c.name == "Modo captura de Prelude";
        });
        if (!present && !protection::anyFailed(checks))
            std::cout << "ok     la carpeta disable_ca_verification sola no es el modo captura\n";
        else {
            std::cout << "FALLA  la carpeta disable_ca_verification sola no deberia dar falla\n";
            ++failures;
        }
    }

    // PRODINFO: con emuNAND presente, apagarlo es una falla.
    {
        const std::string sd = makeSd();
        write(sd + "/exosphere.ini", "[exosphere]\nblank_prodinfo_emummc=0\nblank_prodinfo_sysmmc=1\n");
        const auto checks = protection::run(sd);
        expect("blank_prodinfo_emummc en 0 con emuNAND ES una falla", checks,
               "PRODINFO en blanco (emuNAND)", protection::Status::Fail);
    }
    // Sin carpeta emuMMC no aplica, asi que es aviso y no falla.
    {
        const std::string sd = makeSd();
        std::filesystem::remove_all(sd + "/emuMMC");
        write(sd + "/exosphere.ini", "[exosphere]\nblank_prodinfo_sysmmc=1\n");
        const auto checks = protection::run(sd);
        expect("sin emuNAND, blank_prodinfo_emummc ausente es solo aviso", checks,
               "PRODINFO en blanco (emuNAND)", protection::Status::Warning);
    }

    /* dnsBlockingOk decide si la app se atreve a activar la sincronizacion de
       hora del sistema, que hace que la consola intente contactar el NTP de
       Nintendo. Si esto dijera que si cuando el bloqueo no esta, esos intentos
       saldrian de verdad. */
    {
        const std::string sd = makeSd();
        const auto checks = protection::run(sd);
        if (protection::dnsBlockingOk(checks))
            std::cout << "ok     con default.txt bloqueando, el bloqueo DNS se da por activo\n";
        else {
            std::cout << "FALLA  con default.txt bloqueando deberia darse por activo\n";
            ++failures;
        }
    }
    {
        // El MITM apagado deja sin efecto cualquier fichero de hosts.
        const std::string sd = makeSd();
        write(sd + "/atmosphere/config/system_settings.ini", "[atmosphere]\nenable_dns_mitm = u8!0x0\n");
        const auto checks = protection::run(sd);
        if (!protection::dnsBlockingOk(checks))
            std::cout << "ok     con el MITM apagado, el bloqueo DNS NO se da por activo\n";
        else {
            std::cout << "FALLA  con el MITM apagado no deberia darse por activo\n";
            ++failures;
        }
    }
    {
        // default.txt sin reglas de nintendo y sin fichero de arranque: no hay
        // nada bloqueando.
        const std::string sd = makeSd();
        write(sd + "/atmosphere/hosts/default.txt", HOSTS_NO_BLOCK);
        const auto checks = protection::run(sd);
        if (!protection::dnsBlockingOk(checks))
            std::cout << "ok     sin reglas de nintendo, el bloqueo DNS NO se da por activo\n";
        else {
            std::cout << "FALLA  sin reglas de nintendo no deberia darse por activo\n";
            ++failures;
        }
    }
    {
        // Un PRODINFO en blanco que falla no es asunto del bloqueo DNS: no debe
        // impedir la decision que depende solo de ese bloqueo.
        const std::string sd = makeSd();
        write(sd + "/exosphere.ini", "[exosphere]\nblank_prodinfo_emummc=0\nblank_prodinfo_sysmmc=0\n");
        const auto checks = protection::run(sd);
        if (protection::anyFailed(checks) && protection::dnsBlockingOk(checks))
            std::cout << "ok     un fallo de PRODINFO no cuenta como fallo del bloqueo DNS\n";
        else {
            std::cout << "FALLA  el PRODINFO no deberia afectar a dnsBlockingOk\n";
            ++failures;
        }
    }

    std::cout << (failures ? "\nHAY FALLAS: " : "\nTODO OK (0 fallas)");
    if (failures) std::cout << failures;
    std::cout << "\n";
    return failures ? 1 : 0;
}
