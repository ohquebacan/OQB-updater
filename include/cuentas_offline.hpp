#pragma once

#include <string>

/* Vincular las cuentas sin conexión (5.0.2), a partir de linkalho (GPL-3,
   https://github.com/impeeza/linkalho, de rdmrocha y sus continuadores).

   A cada perfil sin cuenta Nintendo se le escribe una vinculación falsa en la
   partida de sistema de las cuentas (0x8000000000000010): así los juegos dejan
   de pedir vincular la cuenta o de quedarse esperando la nube. Como linkalho,
   se vinculan todos: se sobrescribe la vinculacion que hubiera (tambien una
   cuenta Nintendo real). Las partidas no se tocan.

   Antes de escribir se respalda la carpeta su/ de esa partida en la SD. Hay que
   detener los servicios de cuentas, BCAT y OLSC, así que al terminar la consola
   tiene que reiniciarse. */
namespace cuentasOffline {

    struct Resultado {
        int vinculadas = 0;   // perfiles a los que se les puso la vinculación
        int yaTenian = 0;     // tenian una vinculacion (real o de antes): se sobrescribio
        int fallos = 0;
        bool error = false;   // no se pudo abrir la partida de cuentas: no se cambió nada
        std::string respaldo; // carpeta del respaldo en la SD
        bool reiniciar = false;  // se detuvieron los servicios de cuentas: hay que reiniciar
    };

    Resultado vincular();
}
