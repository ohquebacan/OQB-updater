#include "protection.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "constants.hpp"

namespace protection {

    namespace {

        constexpr const char HOSTS_DIR[]       = "/atmosphere/hosts/";
        constexpr const char SYSTEM_SETTINGS[] = "/atmosphere/config/system_settings.ini";
        constexpr const char EXOSPHERE_INI[]   = "/exosphere.ini";
        constexpr const char EMUMMC_DIR[]      = "/emuMMC/";

        std::string readFile(const std::string& path)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f) return "";
            std::ostringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }

        // Quita espacios y comentarios y baja a minusculas, para comparar sin
        // depender del formato exacto con el que este escrito el ini.
        std::string normalize(const std::string& line)
        {
            std::string out;
            for (const char c : line) {
                if (c == ';' || c == '#') break;
                if (!std::isspace(static_cast<unsigned char>(c))) {
                    out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
            }
            return out;
        }

        // Busca "clave=valor" en un ini ya leido. Devuelve false si la clave no
        // aparece o esta comentada: no es lo mismo que aparezca con otro valor.
        bool findIniValue(const std::string& contents, const std::string& key, std::string& out_value)
        {
            std::istringstream ss(contents);
            std::string line;
            const std::string want = normalize(key) + "=";
            while (std::getline(ss, line)) {
                const std::string n = normalize(line);
                if (n.rfind(want, 0) == 0) {
                    out_value = n.substr(want.length());
                    return true;
                }
            }
            return false;
        }

        bool hostsFileBlocks(const std::string& path, std::string& out_detail)
        {
            if (!std::filesystem::exists(path)) {
                out_detail = "no existe";
                return false;
            }

            const std::string contents = readFile(path);
            if (contents.empty()) {
                out_detail = "esta vacio";
                return false;
            }

            // Contar solo lineas utiles: ni vacias ni comentadas.
            int rules = 0;
            bool blocks_nintendo = false;
            std::istringstream ss(contents);
            std::string line;
            while (std::getline(ss, line)) {
                const std::string n = normalize(line);
                if (n.empty()) continue;
                ++rules;
                // La regla que de verdad importa: cualquier cosa de nintendo
                // mandada a una direccion que no sale a internet.
                if (n.find("nintendo") != std::string::npos &&
                    (n.rfind("127.0.0.1", 0) == 0 || n.rfind("0.0.0.0", 0) == 0)) {
                    blocks_nintendo = true;
                }
            }

            if (rules == 0) {
                out_detail = "solo tiene comentarios";
                return false;
            }
            if (!blocks_nintendo) {
                out_detail = std::to_string(rules) + " reglas, pero ninguna bloquea nintendo";
                return false;
            }

            out_detail = std::to_string(rules) + " reglas";
            return true;
        }

    }  // namespace

    std::vector<Check> run()
    {
        std::vector<Check> checks;

        /* 1. El interruptor general. Si falta, se depende del valor por defecto
              de Atmosphere, y eso ya nos dejo sin bloqueo una vez: Prelude lo
              pone en 0 al salir del modo Nintendo y no lo revierte. */
        {
            const std::string ini = readFile(SYSTEM_SETTINGS);
            std::string value;
            if (ini.empty()) {
                checks.push_back({"DNS MITM activado", Status::Fail, "no se pudo leer system_settings.ini"});
            }
            else if (!findIniValue(ini, "enable_dns_mitm", value)) {
                checks.push_back({"DNS MITM activado", Status::Fail,
                                  "enable_dns_mitm no aparece: se depende del valor por defecto"});
            }
            else if (value.find("0x1") == std::string::npos) {
                checks.push_back({"DNS MITM activado", Status::Fail, "enable_dns_mitm = " + value});
            }
            else {
                checks.push_back({"DNS MITM activado", Status::Ok, "enable_dns_mitm = " + value});
            }
        }

        /* 2. Los tres ficheros de hosts. Atmosphere elige uno segun arranques de
              emuNAND o de sysNAND, asi que los tres tienen que bloquear; con
              tener solo default.txt ya hubo consolas desprotegidas. */
        for (const char* name : {"default.txt", "emummc.txt", "sysmmc.txt"}) {
            std::string detail;
            const bool ok = hostsFileBlocks(std::string(HOSTS_DIR) + name, detail);
            checks.push_back({std::string("hosts/") + name, ok ? Status::Ok : Status::Fail, detail});
        }

        /* 3. PRODINFO en blanco. Es la proteccion que no depende del DNS: aunque
              algo se escape, sin certificado la consola no puede identificarse. */
        {
            const std::string ini = readFile(EXOSPHERE_INI);
            if (ini.empty()) {
                checks.push_back({"PRODINFO en blanco", Status::Fail, "no se pudo leer exosphere.ini"});
            }
            else {
                const bool has_emummc = std::filesystem::exists(EMUMMC_DIR);
                for (const auto& [key, label, required] : {
                         std::tuple<const char*, const char*, bool>{"blank_prodinfo_emummc", "PRODINFO en blanco (emuNAND)", has_emummc},
                         std::tuple<const char*, const char*, bool>{"blank_prodinfo_sysmmc", "PRODINFO en blanco (sysNAND)", false}}) {
                    std::string value;
                    if (!findIniValue(ini, key, value)) {
                        checks.push_back({label, required ? Status::Fail : Status::Warning,
                                          std::string(key) + " no aparece"});
                    }
                    else if (value != "1") {
                        checks.push_back({label, required ? Status::Fail : Status::Warning,
                                          std::string(key) + " = " + value});
                    }
                    else {
                        checks.push_back({label, Status::Ok, std::string(key) + " = 1"});
                    }
                }
            }
        }

        return checks;
    }

    bool anyFailed(const std::vector<Check>& checks)
    {
        return std::any_of(checks.begin(), checks.end(),
                           [](const Check& c) { return c.status == Status::Fail; });
    }

}  // namespace protection
