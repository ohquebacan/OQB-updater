#pragma once

// Trucos (desde Bacán Shop): por cada juego instalado, que version tienes y si
// la base de trucos trae trucos para ESA version. Lo que mas confunde es tener
// el juego en una version para la que no hay trucos: aqui se ve de un vistazo
// y se dice que version los tiene.
//
// De donde sale cada dato, sin pedir nada por juego:
// - la version instalada: ns (la misma que usa la descarga de trucos por juego);
// - su build: versions.json de la base (3,6 MB), guardado en la SD y renovado
//   solo cuando sale una base nueva;
// - que builds tienen trucos: los archivos que la base dejo en
//   atmosphere/contents/<juego>/cheats/ al bajarla.
// La lectura de los juegos corre en un hilo que solo llena datos; la pantalla
// se dibuja siempre desde el hilo de la interfaz.
namespace trucos {
    void abrir();
}
