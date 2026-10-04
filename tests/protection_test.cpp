// Pruebas de protection::run(). Compilan el codigo real, no una copia: la
// funcion acepta una raiz, asi que se le arma una SD de mentira en /tmp.
//
//   c++ -std=c++17 -Iinclude -o /tmp/pt tests/protection_test.cpp source/protection.cpp && /tmp/pt

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "protection.hpp"

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
                                  ("oqb-prot-" + std::to_string(++n))).string();
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
            case protection::Status::Ok: return "Ok";
            case protection::Status::Warning: return "Warning";
            case protection::Status::Fail: return "Fail";
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

    std::cout << (failures ? "\nHAY FALLAS: " : "\nTODO OK (0 fallas)");
    if (failures) std::cout << failures;
    std::cout << "\n";
    return failures ? 1 : 0;
}
