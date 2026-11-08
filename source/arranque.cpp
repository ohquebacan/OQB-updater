#include "arranque.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <vector>

#include "constants.hpp"

namespace arranque {
    namespace {
        std::mutex candado;
        std::vector<std::string> lineas;

        std::chrono::steady_clock::time_point inicio()
        {
            static const auto t = std::chrono::steady_clock::now();
            return t;
        }
    }  // namespace

    void marca(const std::string& fase)
    {
        const auto t0 = inicio();
        const float s = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
        char tiempo[16];
        std::snprintf(tiempo, sizeof(tiempo), "%6.2fs ", s);
        std::lock_guard<std::mutex> l(candado);
        lineas.push_back(tiempo + fase);
        /* Tambien al disco en el momento, linea por linea: si la app se cae
           antes de armar la ventana, el log dice hasta donde llego (antes solo
           se guardaba al final, y un crash al arrancar no dejaba rastro). */
        static bool primera = true;
        if (FILE* f = std::fopen((std::string(CONFIG_PATH) + "arranque.log").c_str(), primera ? "w" : "a")) {
            std::fprintf(f, "%s%s\n", tiempo, fase.c_str());
            std::fclose(f);
            primera = false;
        }
    }

    void guardar()
    {
        std::lock_guard<std::mutex> l(candado);
        std::ofstream f(std::string(CONFIG_PATH) + "arranque.log", std::ios::trunc);
        for (const auto& linea : lineas)
            f << linea << '\n';
    }
}  // namespace arranque
