#pragma once

#include <borealis.hpp>
#include <chrono>
#include <deque>
#include <string>
#include <thread>

typedef std::function<void()> worker_func_t;

class WorkerPage : public brls::View
{
private:
    brls::ProgressDisplay* progressDisp = nullptr;
    brls::StagedAppletFrame* frame;
    brls::Button* button;
    brls::Label* label;
    std::chrono::steady_clock::time_point inicioAnimacion;
    // Las tres lineas en cascada: la mas nueva primero; una cuarta mientras
    // sale. `cambio` es cuando entro la ultima, para la animacion.
    std::deque<std::string> cascada;
    std::chrono::steady_clock::time_point cambio {};
    void drawCascada(NVGcontext* vg, float cx, float y, float ancho, brls::FrameContext* ctx);
    std::string text;
    int progressValue = 0;
    bool workStarted = false;
    bool draw_page = true;
    int nivelesAlVolver = 1;
    std::thread* workerThread;
    worker_func_t workerFunc;

public:
    WorkerPage(brls::StagedAppletFrame* frame, const std::string& text, worker_func_t worker_func);

    /* Cuantas vistas cerrar si falla o se cancela, para volver al punto desde
       el que se abrio el flujo (1: solo este marco). */
    WorkerPage* volverNiveles(int n)
    {
        this->nivelesAlVolver = n;
        return this;
    }
    ~WorkerPage();

    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
    void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override;
    void doWork();
    brls::View* getDefaultFocus() override;
};