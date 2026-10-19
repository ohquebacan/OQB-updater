// Pruebas de crash_report. Compilan el codigo real con informes de mentira
// armados con el formato de Atmosphere (copiado de informes reales).
//
//   c++ -std=c++17 -Iinclude -o /tmp/ct tests/crash_report_test.cpp source/crash_report.cpp && /tmp/ct

#include "crash_report.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

    int failures = 0;

    void check(bool ok, const std::string& what)
    {
        std::cout << (ok ? "ok     " : "FALLA  ") << what << "\n";
        if (!ok) ++failures;
    }

    std::string informe(const std::string& programId, const std::string& proceso, const std::string& result,
                        const std::string& pc)
    {
        return "Atmosphère Crash Report (v1.7):\n"
               "Result:                          " + result + "\n\n"
               "Process Info:\n"
               "    Process Name:                " + proceso + "\n"
               "    Program ID:                  " + programId + "\n"
               "    Process ID:                  000000000000008a\n"
               "Crashed Thread Info:\n"
               "    Registers:\n"
               "        LR:                      000000003feecb60 (tinfoil + 0x2ecb60)\n"
               "        PC:                      " + pc + "\n";
    }

    void write(const std::string& path, const std::string& contents)
    {
        std::ofstream(path, std::ios::binary) << contents;
    }

}  // namespace

int main()
{
    {
        crash::Report r;
        const bool ok = crash::parse("01792357843_050000baddad0000.log",
                                     informe("050000baddad0000", "Application", "0x4A8 (2168-0002)",
                                             "000000003feeca74 (tinfoil + 0x2eca74)"),
                                     r);
        check(ok, "lee un informe de Atmosphere");
        check(r.programId == "050000baddad0000", "saca el Program ID");
        check(r.result == "2168-0002", "saca el codigo de error");
        check(r.module == "tinfoil", "saca el modulo del PC");
        check(r.timestamp == 1792357843ULL, "saca la fecha del nombre del archivo");
        check(r.processName == "Application", "saca el nombre del proceso");
    }
    {
        crash::Report r;
        crash::parse("01790572018_0100000000001000.log",
                     informe("0100000000001000", "qlaunch", "0x4A8 (2168-0002)", "0000006f37baa7cc ([26cc3bc5] + 0x1aa7cc)"), r);
        check(r.module.empty(), "un modulo sin nombre ([hash]) no se toma como nombre");
    }
    {
        crash::Report r;
        check(!crash::parse("x.log", "no es un informe", r), "un archivo cualquiera no se toma como informe");
    }

    // Carpeta de mentira: dos de Tinfoil, uno del menu HOME, un .jpg y dumps/.
    const std::string dir = (std::filesystem::temp_directory_path() / "oqb-crash-test").string();
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir + "/dumps");
    write(dir + "/01790966600_050000baddad0000.log",
          informe("050000baddad0000", "Application", "0x4A8 (2168-0002)", "000000002faad404 (tinfoil + 0x8ad404)"));
    write(dir + "/01792357843_050000baddad0000.log",
          informe("050000BADDAD0000", "Application", "0x4A8 (2168-0002)", "000000003feeca74 (tinfoil + 0x2eca74)"));
    write(dir + "/01790572018_0100000000001000.log",
          informe("0100000000001000", "qlaunch", "0x4A8 (2168-0002)", "0000006f37baa7cc ([26cc3bc5] + 0x1aa7cc)"));
    write(dir + "/01792357843_050000baddad0000.jpg", std::string(100, 'x'));
    write(dir + "/dumps/algo.bin", "x");

    const auto reports = crash::readAll(dir);
    check(reports.size() == 3, "lee los tres .log y no el .jpg");
    check(!reports.empty() && reports[0].timestamp == 1792357843ULL, "el mas nuevo va primero");

    const auto grupos = crash::group(reports);
    check(grupos.size() == 2, "agrupa por programa (mayusculas o minusculas da igual)");
    check(!grupos.empty() && grupos[0].programId == "050000baddad0000" && grupos[0].count == 2,
          "Tinfoil: dos veces, primero porque fallo mas recientemente");
    check(grupos.size() > 1 && !crash::hint(grupos[1]).empty() &&
              crash::hint(grupos[1]).find("menú HOME") != std::string::npos,
          "el menu HOME trae su causa probable");

    check(crash::totalSize(dir) > 100, "cuenta el tamano de .log y .jpg");
    check(crash::removeAll(dir) == 4, "borra los tres .log y el .jpg");
    check(std::filesystem::exists(dir + "/dumps/algo.bin"), "no toca dumps/");
    check(crash::readAll(dir).empty(), "la carpeta queda sin informes");

    std::cout << (failures ? "\nHAY FALLAS: " : "\nTODO OK (0 fallas)");
    if (failures) std::cout << failures;
    std::cout << "\n";
    return failures ? 1 : 0;
}
