// Prueba de hekate_idioma: el idioma del Inicio OQB de hekate según la consola.
//   c++ -std=c++17 -Iinclude -o /tmp/hi tests/hekate_idioma_test.cpp source/hekate_idioma.cpp && /tmp/hi
#include "hekate_idioma.hpp"

#include <iostream>
#include <string>

static int fallas = 0;

static void igual(const std::string& que, const std::string& obtenido, const std::string& esperado)
{
    if (obtenido == esperado) {
        std::cout << "ok     " << que << "\n";
    }
    else {
        std::cout << "FALLA  " << que << "\n  obtenido: [" << obtenido << "]\n  esperado: [" << esperado << "]\n";
        ++fallas;
    }
}

int main()
{
    using namespace hekateIdioma;
    igual("es-419 es español", std::to_string(desdeLocale("es-419")), "0");
    igual("es es español", std::to_string(desdeLocale("es")), "0");
    igual("pt-BR es portugués", std::to_string(desdeLocale("pt-BR")), "2");
    igual("pt es portugués", std::to_string(desdeLocale("pt")), "2");
    igual("en-US es inglés", std::to_string(desdeLocale("en-US")), "1");
    igual("fr va en inglés", std::to_string(desdeLocale("fr")), "1");
    igual("ja va en inglés", std::to_string(desdeLocale("ja")), "1");

    // El nyx.ini del pack: fines de línea mezclados, oqblang en la última línea con LF.
    const std::string pack = "[config]\r\nthemebg=161616\nthemecolor=40\nentries5col=0\r\noqbicons=1\noqblang=1\n";
    igual("lee el oqblang del pack", std::to_string(leer(pack)), "1");
    igual("cambia solo el valor", conIdioma(pack, 0),
          "[config]\r\nthemebg=161616\nthemecolor=40\nentries5col=0\r\noqbicons=1\noqblang=0\n");

    const std::string crlf = "[config]\r\noqblang=1\r\nbpmpclock=1\r\n";
    igual("conserva CRLF de la línea", conIdioma(crlf, 2), "[config]\r\noqblang=2\r\nbpmpclock=1\r\n");

    const std::string sinClave = "[config]\r\nthemebg=161616\r\n";
    igual("sin la clave se lee -1", std::to_string(leer(sinClave)), "-1");
    igual("sin la clave se agrega al final de [config]", conIdioma(sinClave, 2), "[config]\r\nthemebg=161616\r\noqblang=2\r\n");

    const std::string otraSeccion = "[config]\nthemebg=1\n[otra]\noqblang=1\n";
    igual("oqblang de otra sección no cuenta", std::to_string(leer(otraSeccion)), "-1");
    igual("se agrega en [config], no en la otra", conIdioma(otraSeccion, 0), "[config]\nthemebg=1\noqblang=0\n[otra]\noqblang=1\n");

    const std::string sinConfig = "themebg=1\n";
    igual("sin [config] no se toca", conIdioma(sinConfig, 0), sinConfig);

    std::cout << (fallas ? "HAY FALLAS" : "TODO OK") << " (" << fallas << " fallas)\n";
    return fallas ? 1 : 0;
}
