#pragma once

#include <borealis.hpp>

// Iconos de linea dibujados con nanovg (sin imagenes): los del menu lateral y
// los de Inicio. Se dibujan centrados en (cx, cy) dentro de un cuadrado de
// `lado`, con trazo del color dado.
namespace iconos {

    enum class Icono { Inicio, Packs, Apps, Shop, Nextendo, Herramientas, Trucos, Partidas, Escudo, Descarga };

    inline void dibujar(NVGcontext* vg, Icono icono, float cx, float cy, float lado, NVGcolor color)
    {
        const float s = lado / 24.0f;  // los trazos estan pensados en una caja de 24
        auto X = [&](float v) { return cx + (v - 12.0f) * s; };
        auto Y = [&](float v) { return cy + (v - 12.0f) * s; };
        nvgStrokeColor(vg, color);
        nvgStrokeWidth(vg, 2.0f * s);
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);
        nvgBeginPath(vg);
        switch (icono) {
            case Icono::Inicio:
                nvgMoveTo(vg, X(3), Y(11)); nvgLineTo(vg, X(12), Y(4)); nvgLineTo(vg, X(21), Y(11));
                nvgMoveTo(vg, X(5), Y(10)); nvgLineTo(vg, X(5), Y(20)); nvgLineTo(vg, X(19), Y(20)); nvgLineTo(vg, X(19), Y(10));
                break;
            case Icono::Packs:
                nvgMoveTo(vg, X(12), Y(3)); nvgLineTo(vg, X(21), Y(8)); nvgLineTo(vg, X(12), Y(13)); nvgLineTo(vg, X(3), Y(8)); nvgClosePath(vg);
                nvgMoveTo(vg, X(3), Y(13)); nvgLineTo(vg, X(12), Y(18)); nvgLineTo(vg, X(21), Y(13));
                break;
            case Icono::Apps:
                nvgRoundedRect(vg, X(4), Y(4), 7 * s, 7 * s, 1.5f * s);
                nvgRoundedRect(vg, X(13), Y(4), 7 * s, 7 * s, 1.5f * s);
                nvgRoundedRect(vg, X(4), Y(13), 7 * s, 7 * s, 1.5f * s);
                nvgRoundedRect(vg, X(13), Y(13), 7 * s, 7 * s, 1.5f * s);
                break;
            case Icono::Shop:
                nvgMoveTo(vg, X(5), Y(8)); nvgLineTo(vg, X(19), Y(8)); nvgLineTo(vg, X(18), Y(20)); nvgLineTo(vg, X(6), Y(20)); nvgClosePath(vg);
                nvgMoveTo(vg, X(9), Y(8)); nvgArc(vg, X(12), Y(8), 3 * s, NVG_PI, 0, NVG_CW);
                break;
            case Icono::Nextendo:
                nvgCircle(vg, X(12), Y(12), 8 * s);
                nvgMoveTo(vg, X(12), Y(4)); nvgLineTo(vg, X(12), Y(20));
                nvgMoveTo(vg, X(4), Y(12)); nvgLineTo(vg, X(20), Y(12));
                break;
            case Icono::Herramientas:
                nvgMoveTo(vg, X(14), Y(6)); nvgLineTo(vg, X(6), Y(14)); nvgLineTo(vg, X(10), Y(18)); nvgLineTo(vg, X(18), Y(10));
                nvgCircle(vg, X(17), Y(7), 3 * s);
                break;
            case Icono::Trucos:
                nvgMoveTo(vg, X(12), Y(3)); nvgLineTo(vg, X(14.6f), Y(8.6f)); nvgLineTo(vg, X(20.6f), Y(9.2f)); nvgLineTo(vg, X(16.1f), Y(13.2f));
                nvgLineTo(vg, X(17.4f), Y(19.2f)); nvgLineTo(vg, X(12), Y(16)); nvgLineTo(vg, X(6.6f), Y(19.2f)); nvgLineTo(vg, X(7.9f), Y(13.2f));
                nvgLineTo(vg, X(3.4f), Y(9.2f)); nvgLineTo(vg, X(9.4f), Y(8.6f)); nvgClosePath(vg);
                break;
            case Icono::Partidas:
                nvgMoveTo(vg, X(5), Y(4)); nvgLineTo(vg, X(16), Y(4)); nvgLineTo(vg, X(19), Y(7)); nvgLineTo(vg, X(19), Y(20)); nvgLineTo(vg, X(5), Y(20)); nvgClosePath(vg);
                nvgMoveTo(vg, X(8), Y(4)); nvgLineTo(vg, X(8), Y(9)); nvgLineTo(vg, X(15), Y(9)); nvgLineTo(vg, X(15), Y(4));
                nvgMoveTo(vg, X(8), Y(20)); nvgLineTo(vg, X(8), Y(14)); nvgLineTo(vg, X(16), Y(14)); nvgLineTo(vg, X(16), Y(20));
                break;
            case Icono::Escudo:
                nvgMoveTo(vg, X(12), Y(3)); nvgLineTo(vg, X(20), Y(6)); nvgLineTo(vg, X(20), Y(12));
                nvgBezierTo(vg, X(20), Y(17), X(16), Y(20), X(12), Y(21));
                nvgBezierTo(vg, X(8), Y(20), X(4), Y(17), X(4), Y(12));
                nvgLineTo(vg, X(4), Y(6)); nvgClosePath(vg);
                break;
            case Icono::Descarga:
                nvgMoveTo(vg, X(12), Y(3)); nvgLineTo(vg, X(12), Y(15));
                nvgMoveTo(vg, X(7), Y(10)); nvgLineTo(vg, X(12), Y(15)); nvgLineTo(vg, X(17), Y(10));
                nvgMoveTo(vg, X(4), Y(20)); nvgLineTo(vg, X(20), Y(20));
                break;
        }
        nvgStroke(vg);
    }

}  // namespace iconos
