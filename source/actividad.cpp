#include "actividad.hpp"

#include <borealis.hpp>
#include <deque>
#include <mutex>

namespace actividad {
    namespace {
        std::mutex candado;
        std::deque<std::string> pasos;
        std::string detalleActual;
        bool detalleNuevo = false;
        constexpr size_t MAX_PASOS = 12;
    }  // namespace

    void paso(const std::string& texto)
    {
        std::lock_guard<std::mutex> l(candado);
        // Un detalle que no llegó a verse es anterior a este paso: mostrarlo
        // después desordenaría la cascada.
        detalleNuevo = false;
        pasos.push_back(texto);
        while (pasos.size() > MAX_PASOS)
            pasos.pop_front();
    }

    void detalle(const std::string& texto)
    {
        std::lock_guard<std::mutex> l(candado);
        detalleActual = texto;
        detalleNuevo = true;
    }

    void limpiar()
    {
        std::lock_guard<std::mutex> l(candado);
        pasos.clear();
        detalleActual.clear();
        detalleNuevo = false;
    }

    bool siguiente(std::string& out)
    {
        std::lock_guard<std::mutex> l(candado);
        if (!pasos.empty()) {
            out = pasos.front();
            pasos.pop_front();
            return true;
        }
        if (detalleNuevo) {
            out = detalleActual;
            detalleNuevo = false;
            return true;
        }
        return false;
    }

    std::string texto(const char* clave, const std::string& valor)
    {
        return brls::i18n::getStr(std::string("menus/registro/") + clave, valor);
    }

    std::string texto(const char* clave, const std::string& valor, const std::string& otro)
    {
        return brls::i18n::getStr(std::string("menus/registro/") + clave, valor, otro);
    }

    std::string nombre(const std::string& ruta)
    {
        std::string r = ruta.substr(0, ruta.find('?'));
        while (!r.empty() && r.back() == '/')
            r.pop_back();
        const auto barra = r.find_last_of('/');
        return barra == std::string::npos ? r : r.substr(barra + 1);
    }
}  // namespace actividad
