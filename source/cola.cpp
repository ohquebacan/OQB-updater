#include "cola.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include <switch.h>

#include "actividad.hpp"
#include "led.hpp"
#include "progress_event.hpp"
#include "utils.hpp"

namespace cola {

    namespace {

        struct Trabajo {
            int id = 0;
            std::string nombre;
            std::vector<Paso> pasos;
            std::function<std::vector<std::string>()> accesos;
        };

        std::mutex m;
        std::condition_variable cv;
        std::deque<Trabajo> espera;
        std::vector<Hecho> hechos;
        std::deque<Oferta> ofertas;
        std::thread* hilo = nullptr;
        bool parar = false;
        bool trabajando = false;
        int exclusivas = 0;
        int ultimoId = 0;
        int actualId = 0;

        // Lo que se ve de lo actual (bajo m).
        std::string actual, pasoTexto, detalle;
        int pasoNum = 0, pasosTotal = 0;

        void anotar(Hecho h)
        {
            hechos.insert(hechos.begin(), std::move(h));
            if (hechos.size() > 20) hechos.resize(20);
        }

        void bucle()
        {
            for (;;) {
                Trabajo t;
                {
                    std::unique_lock<std::mutex> l(m);
                    // Espera algo que hacer y que ninguna pantalla completa tenga la consola.
                    cv.wait(l, [] { return parar || (!espera.empty() && exclusivas == 0); });
                    if (parar) return;
                    t = std::move(espera.front());
                    espera.pop_front();
                    trabajando = true;
                    actual = t.nombre;
                    actualId = t.id;
                    pasoTexto.clear();
                    detalle.clear();
                    pasoNum = 0;
                    pasosTotal = 0;
                    for (const auto& p : t.pasos) pasosTotal += !p.texto.empty();
                }
                // Que la consola no se duerma a mitad de una descarga.
                appletSetMediaPlaybackState(true);
                led::pulso();

                std::vector<std::function<void()>> despues;
                Hecho h;
                h.id = t.id;
                h.nombre = t.nombre;
                for (size_t i = 0; i < t.pasos.size(); ++i) {
                    /* Un paso sin texto es interno (precalentar): corre despues de dar
                       por terminado el trabajo, para que la pantalla diga listo ya. */
                    if (t.pasos[i].texto.empty()) {
                        if (t.pasos[i].fn) despues.push_back(t.pasos[i].fn);
                        continue;
                    }
                    {
                        std::lock_guard<std::mutex> l(m);
                        pasoTexto = t.pasos[i].texto;
                        ++pasoNum;
                    }
                    auto& p = ProgressEvent::instance();
                    p.reset();
                    actividad::limpiar();
                    if (t.pasos[i].fn) t.pasos[i].fn();
                    p.setStep(p.getMax());
                    if (p.getInterupt()) {
                        h.cancelado = true;
                        break;
                    }
                    if (p.getStatusCode() > 399) {
                        h.error = util::getErrorMessage(p.getStatusCode());
                        break;
                    }
                }
                ProgressEvent::instance().setInterupt(false);
                ProgressEvent::instance().setStatusCode(0);

                std::vector<std::string> nros;
                if (h.error.empty() && !h.cancelado && t.accesos) nros = t.accesos();
                bool vacia;
                {
                    std::lock_guard<std::mutex> l(m);
                    anotar(h);
                    if (!nros.empty()) ofertas.push_back({h.nombre, nros});
                    trabajando = false;
                    actual.clear();
                    actualId = 0;
                    vacia = espera.empty();
                }
                if (h.error.empty() && !h.cancelado)
                    for (auto& fn : despues) fn();
                cv.notify_all();  // una pantalla completa puede estar esperando
                if (vacia) {
                    appletSetMediaPlaybackState(false);
                    if (h.error.empty() && !h.cancelado)
                        led::exito();
                    else
                        led::apagar();
                }
            }
        }

    }  // namespace

    int agregar(const std::string& nombre, std::vector<Paso> pasos, std::function<std::vector<std::string>()> accesos)
    {
        int id;
        {
            std::lock_guard<std::mutex> l(m);
            id = ++ultimoId;
            espera.push_back({id, nombre, std::move(pasos), std::move(accesos)});
            if (!hilo) {
                parar = false;
                hilo = new std::thread(bucle);
            }
        }
        cv.notify_all();
        return id;
    }

    bool siguienteOferta(Oferta& out)
    {
        std::lock_guard<std::mutex> l(m);
        if (ofertas.empty()) return false;
        out = std::move(ofertas.front());
        ofertas.pop_front();
        return true;
    }

    bool ocupada()
    {
        std::lock_guard<std::mutex> l(m);
        return trabajando || !espera.empty();
    }

    Estado estado()
    {
        Estado e;
        std::lock_guard<std::mutex> l(m);
        e.trabajando = trabajando;
        e.actual = actual;
        e.actualId = actualId;
        e.paso = pasoTexto;
        e.detalle = detalle;
        e.pasoNum = pasoNum;
        e.pasos = pasosTotal;
        if (trabajando) {
            auto& p = ProgressEvent::instance();
            e.ahora = p.getNow();
            e.total = p.getTotal();
            e.velocidad = p.getSpeed();
            e.step = p.getStep();
            e.max = p.getMax();
        }
        for (const auto& t : espera) {
            e.espera.push_back(t.nombre);
            e.esperaIds.push_back(t.id);
        }
        e.hechos = hechos;
        return e;
    }

    bool siguienteLinea(std::string& out)
    {
        {
            std::lock_guard<std::mutex> l(m);
            if (!trabajando) return false;
        }
        if (!actividad::siguiente(out)) return false;
        std::lock_guard<std::mutex> l(m);
        detalle = out;
        return true;
    }

    void cancelarActual()
    {
        std::lock_guard<std::mutex> l(m);
        if (trabajando) ProgressEvent::instance().setInterupt(true);
    }

    void quitar(std::size_t i)
    {
        std::lock_guard<std::mutex> l(m);
        if (i < espera.size()) espera.erase(espera.begin() + i);
    }

    void mover(std::size_t i, int hacia)
    {
        std::lock_guard<std::mutex> l(m);
        const long j = (long)i + hacia;
        if (i < espera.size() && j >= 0 && j < (long)espera.size()) std::swap(espera[i], espera[j]);
    }

    void limpiarHechos()
    {
        std::lock_guard<std::mutex> l(m);
        hechos.clear();
    }

    bool tomarExclusiva()
    {
        std::lock_guard<std::mutex> l(m);
        if (trabajando || !espera.empty()) return false;
        ++exclusivas;
        return true;
    }

    void soltarExclusiva()
    {
        {
            std::lock_guard<std::mutex> l(m);
            if (exclusivas > 0) --exclusivas;
        }
        cv.notify_all();
    }

    void detener()
    {
        std::thread* h;
        {
            std::lock_guard<std::mutex> l(m);
            parar = true;
            espera.clear();
            if (trabajando) ProgressEvent::instance().setInterupt(true);
            h = hilo;
            hilo = nullptr;
        }
        cv.notify_all();
        if (h) {
            if (h->joinable()) h->join();
            delete h;
        }
        appletSetMediaPlaybackState(false);
    }

}  // namespace cola
