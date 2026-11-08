#pragma once

#include <borealis.hpp>

// El icono de carga de la app PC: la Trifuerza con los tres triangulos
// encendiendose en secuencia. `t` en segundos; centrado en (cx, cy). `escala`
// multiplica el viewBox 36x32 del SVG; `completa` enciende los tres (carga
// terminada).
void drawIconoCarga(NVGcontext* vg, float cx, float cy, float t, float escala = 2.4f, bool completa = false);

// La Trifuerza del arranque: los triangulos se encienden uno tras otro y se
// quedan encendidos (arriba, abajo derecha, abajo izquierda). `t` en segundos
// desde que se ve la pantalla; a partir de duracionEncendido() estan los tres.
void drawTrifuerzaEncendido(NVGcontext* vg, float cx, float cy, float t, float escala);
float duracionEncendido();
