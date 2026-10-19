#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Lectura de los informes de error que Atmosphere deja en
// atmosphere/crash_reports, para resumirlos sin tener que abrirlos uno por uno.
//
// La lectura no depende de la consola, asi que se prueba en la Mac con informes
// de mentira; poner nombre a cada programa (forwarders, juegos) si, y va aparte.
namespace crash {

    struct Report
    {
        std::string file;         // nombre del .log
        uint64_t timestamp = 0;   // segundos desde 1970, sale del nombre del archivo
        std::string programId;    // 16 hex en minusculas
        std::string processName;  // "Application", "qlaunch", "Forwarder"...
        std::string result;       // "2168-0002", o vacio si no aparece
        std::string module;       // modulo donde estaba el PC ("tinfoil", "hbl"...)
    };

    // Lee un informe ya cargado. Devuelve false si no parece un informe de
    // Atmosphere (falta el Program ID).
    bool parse(const std::string& file, const std::string& contents, Report& out);

    // Todos los informes de una carpeta, del mas nuevo al mas viejo.
    std::vector<Report> readAll(const std::string& dir);

    struct Group
    {
        std::string programId;
        std::string processName;
        std::string module;      // del informe mas reciente
        std::string lastResult;  // del informe mas reciente
        uint64_t lastTimestamp = 0;
        int count = 0;
    };

    // Agrupa por programa, del que fallo mas recientemente al que menos.
    std::vector<Group> group(const std::vector<Report>& reports);

    // Causa probable cuando el patron es conocido, o vacio. En espanol: es lo
    // que lee el cliente.
    std::string hint(const Group& g);

    // Fecha legible (dd/mm/aaaa) de un timestamp.
    std::string date(uint64_t timestamp);

    // Lo que ocupan los informes (.log y .jpg) de la carpeta, en bytes.
    uint64_t totalSize(const std::string& dir);

    // Borra los .log y .jpg de la carpeta. No toca subcarpetas (dumps/).
    // Devuelve cuantos archivos borro.
    int removeAll(const std::string& dir);

}  // namespace crash
