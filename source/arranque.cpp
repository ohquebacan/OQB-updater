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
    }

    void guardar()
    {
        std::lock_guard<std::mutex> l(candado);
        std::ofstream f(std::string(CONFIG_PATH) + "arranque.log", std::ios::trunc);
        for (const auto& linea : lineas)
            f << linea << '\n';
    }
}  // namespace arranque
