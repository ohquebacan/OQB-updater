#include "protection.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "constants.hpp"

#ifdef __SWITCH__
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <switch.h>
#endif

namespace protection {

    namespace {

        constexpr const char HOSTS_DIR[] = "/atmosphere/hosts/";
        constexpr const char SYSTEM_SETTINGS[] = "/atmosphere/config/system_settings.ini";
        constexpr const char EXOSPHERE_INI[] = "/exosphere.ini";
        constexpr const char EMUMMC_DIR[] = "/emuMMC/";
        /* La marca que escribe Prelude (4.0.x) al activar su modo captura y
           borra al salir. No sirve la carpeta de parches disable_ca_verification
           que el modo tambien instala: puede quedar de otros usos y daba la
           falla en consolas que no estaban en modo captura. */
        constexpr const char PRELUDE_CAPTURE_FLAG[] = "/switch/prelude_capture_mode.flag";

        /* Los servidores de Nextendo, sacados de Prelude (nextendo.nro): los dos
           que deja elegir (VPS y Local) y uno fijo que usa para algunos hosts.
           Si cambian, la app deja de reconocerlos y lo da como falla a secas,
           que es el lado seguro. */
        constexpr const char* NEXTENDO_SERVERS[] = {"51.178.29.194", "3.135.232.168", "164.132.111.120"};

        bool isNextendoServer(const std::string& ip)
        {
            return std::find(std::begin(NEXTENDO_SERVERS), std::end(NEXTENDO_SERVERS), ip) !=
                   std::end(NEXTENDO_SERVERS);
        }

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

        // Tres resultados, no dos: que un fichero falte no es lo mismo que que
        // exista y no bloquee. Atmosphere usa default.txt cuando no encuentra
        // el fichero del arranque, asi que faltar es lo normal; existir sin
        // bloquear es lo que deja la consola al descubierto.
        enum class HostsResult
        {
            Blocks,
            Absent,
            DoesNotBlock,
            // Manda lo de Nintendo a los servidores de Nextendo.
            RedirectsNextendo,
            // Manda lo de Nintendo a otro servidor publico, que no sabemos cual
            // es: puede ser el de Nintendo.
            RedirectsOther,
        };

        HostsResult inspectHostsFile(const std::string& path, std::string& out_detail)
        {
            if (!std::filesystem::exists(path)) {
                out_detail = "no existe";
                return HostsResult::Absent;
            }

            const std::string contents = readFile(path);
            if (contents.empty()) {
                out_detail = "esta vacio";
                return HostsResult::DoesNotBlock;
            }

            // Contar solo lineas utiles: ni vacias ni comentadas.
            int rules = 0;
            bool blocks_nintendo = false;
            std::string nextendo_ip;
            std::string other_ip;
            std::istringstream ss(contents);
            std::string line;
            while (std::getline(ss, line)) {
                const std::string n = normalize(line);
                if (n.empty()) continue;
                ++rules;
                if (n.find("nintendo") == std::string::npos) continue;
                // La regla que de verdad importa: cualquier cosa de nintendo
                // mandada a una direccion que no sale a internet.
                if (n.rfind("127.", 0) == 0 || n.rfind("0.0.0.0", 0) == 0) {
                    blocks_nintendo = true;
                    continue;
                }
                /* Mandada a un servidor publico. Las de la prueba de conexion
                   no cuentan: el pack OQB manda ctest y conntest a 90DNS a
                   proposito, para que la consola crea que tiene internet. */
                if (n.find("ctest") != std::string::npos || n.find("conntest") != std::string::npos) continue;
                std::istringstream fields(line);
                std::string ip;
                fields >> ip;
                std::string& slot = isNextendoServer(ip) ? nextendo_ip : other_ip;
                if (slot.empty()) slot = ip;
            }

            if (rules == 0) {
                out_detail = "solo tiene comentarios";
                return HostsResult::DoesNotBlock;
            }
            if (!other_ip.empty()) {
                out_detail = "manda lo de nintendo a " + other_ip + ", que no es un servidor de Nextendo";
                return HostsResult::RedirectsOther;
            }
            if (!nextendo_ip.empty()) {
                out_detail = "manda lo de nintendo a Nextendo (" + nextendo_ip + ")";
                return HostsResult::RedirectsNextendo;
            }
            if (!blocks_nintendo) {
                out_detail = std::to_string(rules) + " reglas, pero ninguna bloquea nintendo";
                return HostsResult::DoesNotBlock;
            }

            out_detail = std::to_string(rules) + " reglas";
            return HostsResult::Blocks;
        }

        std::string versionText(uint32_t v)
        {
            return std::to_string((v >> 16) & 0xFF) + "." + std::to_string((v >> 8) & 0xFF) + "." +
                   std::to_string(v & 0xFF);
        }

        constexpr uint32_t version(uint32_t major, uint32_t minor, uint32_t micro)
        {
            return (major << 16) | (minor << 8) | micro;
        }

    }  // namespace

    std::vector<Check> run(const std::string& root, const Runtime& rt)
    {
        std::vector<Check> checks;

        /* 1. El MITM, probado de verdad cuando se puede. Leer el ajuste no
              basta: Prelude lo deja en 0 al volver del modo Nextendo, y el
              Atmosphere del pack OQB lo ignora y deja el MITM siempre puesto,
              asi que el ajuste en 0 no dice nada sobre si se bloquea o no.
              Preguntarle al DNS por un host de Nintendo si lo dice. */
        if (rt.dns == Runtime::Dns::Blocked) {
            checks.push_back({"DNS MITM activado", Status::Ok, "probado: " + rt.dnsDetail, true});
        }
        else if (rt.dns == Runtime::Dns::Redirected) {
            checks.push_back({"DNS MITM activado", Status::Fail,
                              "probado: " + rt.dnsDetail +
                                  (isNextendoServer(rt.dnsAddress) ? ", va a Nextendo"
                                                                   : ", sale a internet y no es Nextendo"),
                              true});
        }
        /* Sin prueba (o sin respuesta, que pasa sin MITM y sin red) queda el
           ajuste. Si falta, se depende del valor por defecto de Atmosphere. */
        else {
            const std::string ini = readFile(root + SYSTEM_SETTINGS);
            std::string value;
            if (ini.empty()) {
                checks.push_back({"DNS MITM activado", Status::Fail, "no se pudo leer system_settings.ini", true});
            }
            else if (!findIniValue(ini, "enable_dns_mitm", value)) {
                checks.push_back({"DNS MITM activado", Status::Fail,
                                  "enable_dns_mitm no aparece: se depende del valor por defecto", true});
            }
            else if (value.find("0x1") == std::string::npos) {
                checks.push_back({"DNS MITM activado", Status::Fail, "enable_dns_mitm = " + value, true});
            }
            else {
                checks.push_back({"DNS MITM activado", Status::Ok, "enable_dns_mitm = " + value, true});
            }
        }

        /* 1b. Que el Atmosphere sepa bloquear en este firmware. Desde 23.0 los
               modulos del sistema resuelven por otro camino que el MITM no
               cubria: la prueba de arriba sale bien (la app usa el camino
               viejo) mientras la consola habla con Nintendo igual. Solo se
               puede saber por la version, y no del todo: 1.12.0 oficial no
               trae el arreglo, pero las compilaciones de borntohonk y las de
               prueba OQB con la misma version si. Por eso 1.12.0 es aviso. */
        if (rt.firmware != 0 && rt.atmosphere != 0 && rt.firmware >= version(23, 0, 0)) {
            const std::string what = "Atmosphère " + versionText(rt.atmosphere) + " en firmware " +
                                     versionText(rt.firmware);
            if (rt.atmosphere >= version(1, 12, 1)) {
                checks.push_back({"Bloqueo DNS en este firmware", Status::Ok, what, true});
            }
            else if (rt.atmosphere == version(1, 12, 0)) {
                checks.push_back({"Bloqueo DNS en este firmware", Status::Warning,
                                  what + ": puede que no bloquee a los módulos del sistema. El pack OQB con 1.12.1 sí lo hace",
                                  true});
            }
            else {
                checks.push_back({"Bloqueo DNS en este firmware", Status::Fail,
                                  what + ": no bloquea a los módulos del sistema. Actualiza el pack", true});
            }
        }

        /* 2. Los ficheros de hosts. Atmosphere busca el del arranque en curso
              (emummc.txt o sysmmc.txt) y, si no existe, usa default.txt. Por eso
              default.txt es el que tiene que bloquear siempre, y los otros dos
              solo importan cuando existen: entonces reemplazan a default.txt y
              son ellos los que deciden. */
        /* Lo que lleva a Nextendo, y lo que sale a otro lado. Lo segundo nunca
           se explica como Nextendo: si la consola llega a un servidor que no es
           el suyo, puede ser el de Nintendo, y eso es una falla de verdad. */
        bool nextendo = rt.dns == Runtime::Dns::Redirected && isNextendoServer(rt.dnsAddress);
        std::vector<std::string> not_nextendo = {"Bloqueo DNS en este firmware"};
        if (rt.dns == Runtime::Dns::Redirected && !nextendo) not_nextendo.push_back("DNS MITM activado");

        /* Que dio cada fichero de hosts, para saber despues si Nextendo esta
           activo en ESTE arranque y no solo configurado para la otra NAND. */
        std::vector<std::pair<std::string, HostsResult>> hosts_results;
        const auto note = [&](const std::string& name, HostsResult r) {
            hosts_results.emplace_back(name, r);
            if (r == HostsResult::RedirectsNextendo) nextendo = true;
            if (r == HostsResult::RedirectsOther) not_nextendo.push_back(name);
        };

        std::string default_detail;
        const HostsResult default_result = inspectHostsFile(root + HOSTS_DIR + "default.txt", default_detail);
        const bool default_blocks = default_result == HostsResult::Blocks;
        note("hosts/default.txt", default_result);
        checks.push_back({"hosts/default.txt", default_blocks ? Status::Ok : Status::Fail, default_detail, true});

        for (const char* name : {"emummc.txt", "sysmmc.txt"}) {
            std::string detail;
            const HostsResult result = inspectHostsFile(root + HOSTS_DIR + name, detail);
            note(std::string("hosts/") + name, result);
            switch (result) {
                case HostsResult::Blocks:
                    checks.push_back({std::string("hosts/") + name, Status::Ok, detail, true});
                    break;

                case HostsResult::Absent:
                    // Faltar es lo normal y no rompe nada: manda default.txt.
                    // Solo es un fallo si default.txt tampoco bloquea, y en ese
                    // caso el fallo de verdad ya esta reportado arriba.
                    checks.push_back({std::string("hosts/") + name,
                                      default_blocks ? Status::Ok : Status::Fail,
                                      default_blocks ? "no existe: manda default.txt, que si bloquea"
                                                     : "no existe, y default.txt tampoco bloquea",
                                      true});
                    break;

                case HostsResult::RedirectsNextendo:
                case HostsResult::RedirectsOther:
                case HostsResult::DoesNotBlock:
                    // El caso peligroso: existe, asi que deja fuera a
                    // default.txt, y no bloquea.
                    checks.push_back({std::string("hosts/") + name, Status::Fail,
                                      detail + " — reemplaza a default.txt", true});
                    break;
            }
        }

        /* 3. PRODINFO en blanco. Es la proteccion que no depende del DNS: aunque
              algo se escape, sin certificado la consola no puede identificarse. */
        {
            const std::string ini = readFile(root + EXOSPHERE_INI);
            if (ini.empty()) {
                checks.push_back({"PRODINFO en blanco", Status::Fail, "no se pudo leer exosphere.ini"});
            }
            else {
                const bool has_emummc = std::filesystem::exists(root + EMUMMC_DIR);
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

        /* Con Nextendo las protecciones se caen a proposito: Prelude cambia los
           hosts para que la consola hable con sus servidores. Se marcan para
           explicarlo en vez de alarmar, pero siguen siendo fallas: la consola
           de verdad esta saliendo, y quien pregunte por el bloqueo (la hora del
           sistema) tiene que saberlo. La version de Atmosphere no es cosa de
           Nextendo, asi que esa no se marca. */
        /* 5. El modo captura de Prelude: servidores reales de Nintendo, sin
              verificar certificados y con el numero de serie a la vista. Prelude
              mismo lo marca como riesgo alto de baneo. No es Nextendo, asi que
              nunca se explica como tal. */
        if (std::filesystem::exists(root + PRELUDE_CAPTURE_FLAG)) {
            checks.push_back({"Modo captura de Prelude", Status::Fail,
                              "Está activo: la consola habla con los servidores reales de Nintendo sin verificar "
                              "certificados. Riesgo alto de baneo. Sal de él en Prelude en cuanto termines",
                              true});
            not_nextendo.push_back("Modo captura de Prelude");
        }

        /* El caso al reves: sin Nextendo, pero con el numero de serie a la
           vista. Suele ser un Prelude que no termino de volver a Nintendo, o
           archivos copiados a mano a medias. */
        if (!nextendo) {
            for (auto& c : checks) {
                if (c.status == Status::Fail && c.name.rfind("PRODINFO", 0) == 0) {
                    c.detail += ". Si usaste Nextendo, Prelude pregunta al volver a Nintendo: si elegiste "
                                "\"Mantener real\", es por eso; vuelve a Prelude y elige \"Blanquear\". Si no, "
                                "puede que el pack se haya copiado desde la computadora sin reemplazar los "
                                "archivos anteriores: reinstálalo desde la app, en Actualizar Atmosphère, que "
                                "reemplaza todo como debe";
                }
            }
        }

        if (nextendo) {
            for (auto& c : checks) {
                if (c.status == Status::Fail &&
                    std::find(not_nextendo.begin(), not_nextendo.end(), c.name) == not_nextendo.end()) {
                    c.byNextendo = true;
                }
            }

            /* En que NAND se esta usando. Exosphere lo dice; si no se pudo
               preguntar (las pruebas), se deduce como antes: con carpeta
               emuMMC se da por emuNAND. */
            const bool sysnand = rt.nand == Runtime::Nand::Sys ||
                                 (rt.nand == Runtime::Nand::Unknown && !std::filesystem::exists(root + EMUMMC_DIR));

            /* Nextendo activo en ESTE arranque, no solo configurado. Puede haber
               un emummc.txt que manda a Nextendo mientras se arranca en sysNAND
               bloqueado: eso no es "Nextendo en sysNAND" ni esta a medias.
               - La prueba de DNS lo dice directo cuando se pudo hacer.
               - Si no, manda el hosts de esta NAND (o default.txt si no existe).
               - Sin saber la NAND (las pruebas), se queda lo de antes. */
            bool here = true;
            if (rt.dns == Runtime::Dns::Blocked) {
                here = false;
            }
            else if (rt.dns == Runtime::Dns::Redirected) {
                here = isNextendoServer(rt.dnsAddress);
            }
            else if (rt.nand != Runtime::Nand::Unknown) {
                const std::string own = rt.nand == Runtime::Nand::Sys ? "hosts/sysmmc.txt" : "hosts/emummc.txt";
                const auto result_of = [&](const std::string& n) {
                    for (const auto& [name, r] : hosts_results)
                        if (name == n) return r;
                    return HostsResult::Absent;
                };
                HostsResult active = result_of(own);
                if (active == HostsResult::Absent) active = result_of("hosts/default.txt");
                here = active == HostsResult::RedirectsNextendo;
            }

            /* 4a. Nextendo en sysNAND. Prelude (4.0.5) escribe sysmmc.txt y
                   emummc.txt a la vez, asi que activarlo desde emuNAND tambien
                   manda la sysNAND a Nextendo, se quiera o no. Y solo destapa el
                   PRODINFO de emuNAND (deja blank_prodinfo_sysmmc=1), asi que en
                   sysNAND lo mas probable es que no funcione. Hay quien lo usa
                   asi a proposito, asi que no se corta nada: solo se avisa. */
            if (here && sysnand) {
                checks.push_back({NEXTENDO_SYSNAND_CHECK, Status::Warning,
                                  "Estás usando Nextendo en la consola original (sysNAND). Te recomendamos no "
                                  "usarlo aquí: la sysNAND es la que se conecta a Nintendo, y usar Nextendo en ella "
                                  "puede aumentar el riesgo de baneo cuando vuelva a conectarse. Úsalo arrancando "
                                  "en emuNAND, que está para esto. Ojo: Prelude activa Nextendo en las dos a la "
                                  "vez, aunque lo hayas activado desde emuNAND. Para dejar la sysNAND limpia, "
                                  "vuelve a Nintendo en Prelude antes de arrancarla (lo apaga en las dos) y vuelve "
                                  "a activarlo cuando entres a emuNAND. Si lo usas en sysNAND a propósito, "
                                  "puedes seguir"});
            }

            /* 4. Nextendo a medias. Prelude cambia dos cosas: los hosts, para
                  mandar la consola a sus servidores, y el PRODINFO, para que la
                  consola muestre su numero de serie y esos servidores la
                  reconozcan. Si solo llego lo primero, Nextendo no funciona. Pasa
                  cuando el pack se copio desde la computadora sin reemplazar y
                  quedo un exosphere.ini de antes; la app si reemplaza. Se mira
                  el PRODINFO de la NAND en la que se arranco. */
            if (here && (sysnand || std::filesystem::exists(root + EMUMMC_DIR))) {
                bool hidden;
                if (rt.blankProdinfo >= 0) {
                    hidden = rt.blankProdinfo == 1;
                }
                else {
                    std::string value;
                    const char* key = sysnand ? "blank_prodinfo_sysmmc" : "blank_prodinfo_emummc";
                    hidden = !findIniValue(readFile(root + EXOSPHERE_INI), key, value) || value == "1";
                }
                if (hidden && sysnand) {
                    /* En sysNAND reinstalar el pack no lo arregla: Prelude solo
                       destapa el PRODINFO de emuNAND. */
                    checks.push_back({NEXTENDO_CHECK, Status::Warning,
                                      "Nextendo está activo aquí, pero la sysNAND no muestra su número de serie, así "
                                      "que lo más probable es que sus servidores no la reconozcan. Es lo esperado: "
                                      "Prelude solo muestra el número de serie en emuNAND. Si Nextendo te funciona "
                                      "igual en sysNAND, puedes ignorar esto. Si no, úsalo arrancando en emuNAND"});
                }
                else if (hidden) {
                    checks.push_back({NEXTENDO_CHECK, Status::Warning,
                                      "Nextendo está activo, pero la consola no muestra su número de serie, así que "
                                      "sus servidores no la van a reconocer. Esto pasa cuando el pack se copia a la "
                                      "SD desde la computadora sin reemplazar lo que ya había: los archivos nuevos se "
                                      "mezclan con los de packs anteriores. Reinstala el HATS PACK desde la app, en "
                                      "Actualizar Atmosphère, que reemplaza todo como debe, y luego vuelve a activar "
                                      "Nextendo en Prelude"});
                }
                else {
                    checks.push_back({NEXTENDO_CHECK, Status::Ok,
                                      "Va a sus servidores y muestra el número de serie, como necesita"});
                }
            }
        }

        return checks;
    }

#ifdef __SWITCH__
    Runtime probeRuntime()
    {
        Runtime rt;

        // hosversionGet ya viene empaquetado igual que Runtime::firmware.
        rt.firmware = hosversionGet();

        // 65000 es el item de configuracion de Atmosphere con su version.
        u64 ams = 0;
        if (R_SUCCEEDED(splGetConfig(static_cast<SplConfigItem>(65000), &ams))) {
            rt.atmosphere = version((ams >> 56) & 0xFF, (ams >> 48) & 0xFF, (ams >> 40) & 0xFF);
        }

        // 65007: tipo de emuMMC del arranque (0 = sysNAND). 65005: PRODINFO en blanco.
        u64 emummc = 0;
        if (R_SUCCEEDED(splGetConfig(static_cast<SplConfigItem>(65007), &emummc))) {
            rt.nand = emummc == 0 ? Runtime::Nand::Sys : Runtime::Nand::Emu;
        }
        u64 blank = 0;
        if (R_SUCCEEDED(splGetConfig(static_cast<SplConfigItem>(65005), &blank))) {
            rt.blankProdinfo = blank != 0 ? 1 : 0;
        }

        /* Un host que los hosts del pack bloquean y que Atmosphere no bloquea
           por su cuenta (sus reglas por defecto son solo las de telemetria),
           asi que si da 127.0.0.1 es que los hosts de la SD se estan aplicando.
           Con el MITM puesto la respuesta es local e inmediata.

           No sirve ctest.cdn.nintendo.net: el pack lo manda a proposito al
           servidor de prueba de conexion de 90DNS para que la consola crea que
           tiene internet, y eso se leia como que el DNS salia. La cuenta si es
           algo que Nextendo tiene que redirigir, asi que distingue los modos. */
        constexpr const char HOST[] = "accounts.nintendo.com";
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(HOST, nullptr, &hints, &res) != 0 || res == nullptr) {
            rt.dns = Runtime::Dns::Unresolved;
            return rt;
        }

        const auto* sin = reinterpret_cast<const sockaddr_in*>(res->ai_addr);
        const uint32_t ip = ntohl(sin->sin_addr.s_addr);
        char text[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &sin->sin_addr, text, sizeof(text));
        freeaddrinfo(res);

        const bool loopback = (ip >> 24) == 127 || ip == 0;
        rt.dns = loopback ? Runtime::Dns::Blocked : Runtime::Dns::Redirected;
        rt.dnsAddress = text;
        rt.dnsDetail = std::string(HOST) + " → " + text;
        return rt;
    }

    std::vector<Check> runOnConsole()
    {
        return run("", probeRuntime());
    }
#endif

    bool dnsBlockingOk(const std::vector<Check>& checks)
    {
        return std::none_of(checks.begin(), checks.end(), [](const Check& c) {
            return c.dnsRelated && c.status == Status::Fail;
        });
    }

    bool nextendoIncomplete(const std::vector<Check>& checks)
    {
        return std::any_of(checks.begin(), checks.end(), [](const Check& c) {
            return c.name == NEXTENDO_CHECK && c.status == Status::Warning;
        });
    }

    bool nextendoOnSysnand(const std::vector<Check>& checks)
    {
        return std::any_of(checks.begin(), checks.end(), [](const Check& c) {
            return c.name == NEXTENDO_SYSNAND_CHECK && c.status == Status::Warning;
        });
    }

    bool failedOnlyByNextendo(const std::vector<Check>& checks)
    {
        return anyFailed(checks) && std::all_of(checks.begin(), checks.end(), [](const Check& c) {
                   return c.status != Status::Fail || c.byNextendo;
               });
    }

    bool anyFailed(const std::vector<Check>& checks)
    {
        return std::any_of(checks.begin(), checks.end(),
                           [](const Check& c) { return c.status == Status::Fail; });
    }

}  // namespace protection
