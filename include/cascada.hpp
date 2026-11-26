#pragma once

#include <borealis.hpp>
#include <chrono>
#include <deque>
#include <functional>
#include <string>

/* Las lineas en cascada de las descargas (la mas nueva arriba, las anteriores
   bajando y apagandose). La misma animacion que WorkerPage, para las pantallas
   que no son una WorkerPage (la instalacion de un port en la cola, 5.0.0). */
class Cascada
{
public:
    // `siguiente` da la proxima linea (false si no hay); se pide cada 150 ms.
    void dibujar(NVGcontext* vg, float cx, float y, float ancho, brls::FrameContext* ctx, float alfaVista, const std::function<bool(std::string&)>& siguiente);
    void limpiar() { this->cascada.clear(); }

private:
    std::deque<std::string> cascada;
    std::chrono::steady_clock::time_point cambio{};
};
