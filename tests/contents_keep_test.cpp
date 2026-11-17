// Qué carpetas de atmosphere/contents se conservan al instalar un HATS pack.
//   c++ -std=c++17 -Iinclude -o /tmp/ck tests/contents_keep_test.cpp && /tmp/ck
#include "contents_keep.hpp"

#include <iostream>
#include <string>

static int fallas = 0;

static void caso(const std::string& que, bool obtenido, bool esperado)
{
    std::cout << (obtenido == esperado ? "ok     " : "FALLA  ") << que << "\n";
    if (obtenido != esperado) ++fallas;
}

int main()
{
    using contentsKeep::esContenidoDeJuego;
    // Juegos: traducciones, mods, trucos (sin exefs.nsp).
    caso("traducción de un juego (0100...)", esContenidoDeJuego("01006F8002326000", false), true);
    caso("en minúsculas también", esContenidoDeJuego("0100b00b51230000", false), true);
    caso("port/forwarder 05...", esContenidoDeJuego("05C7A4F9F5A56000", false), true);
    caso("parche de juego con exefs/ (carpeta, no .nsp)", esContenidoDeJuego("010048701995E000", false), true);
    // Sysmodules del pack o viejos: se limpian.
    caso("MissionControl (sistema, con exefs.nsp)", esContenidoDeJuego("010000000000bd00", true), false);
    caso("emuiibo (sistema)", esContenidoDeJuego("0100000000000352", true), false);
    caso("menú HOME del sistema sin exefs.nsp", esContenidoDeJuego("0100000000001000", false), false);
    caso("flags de un programa del sistema", esContenidoDeJuego("010000000000000D", false), false);
    caso("sys-clk (00FF..., con exefs.nsp)", esContenidoDeJuego("00FF0000636C6BFF", true), false);
    caso("ovlloader (4200..., con exefs.nsp)", esContenidoDeJuego("420000000007E51A", true), false);
    caso("noexes (054E..., con exefs.nsp)", esContenidoDeJuego("054e4f4558454000", true), false);
    // Nombres raros: ante la duda, como antes.
    caso("nombre que no es un ID", esContenidoDeJuego("traducciones", false), false);
    caso("ID con 15 dígitos", esContenidoDeJuego("01006F800232600", false), false);
    caso("ID con letra inválida", esContenidoDeJuego("01006F80023260XG", false), false);

    std::cout << (fallas ? "HAY FALLAS" : "TODO OK") << " (" << fallas << " fallas)\n";
    return fallas ? 1 : 0;
}
