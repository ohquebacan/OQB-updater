#include "hekate_idioma.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef __SWITCH__
#include <borealis.hpp>
#endif

namespace hekateIdioma {

    namespace {
        constexpr const char NYX_INI[] = "/bootloader/nyx.ini";
        // Marca de que la app ya hizo el ajuste de la primera vez.
        constexpr const char HECHO[] = "/config/aio-switch-updater/hekate-idioma.txt";
        constexpr int INGLES = 1;  // el del pack

        std::string leerArchivo(const std::string& ruta)
        {
            std::ifstream f(ruta, std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }

        bool escribirArchivo(const std::string& ruta, const std::string& datos)
        {
            const std::string tmp = ruta + ".tmp";
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                if (!f) return false;
                f << datos;
                if (!f.good()) return false;
            }
            /* En la Switch, renombrar encima de un archivo que existe falla
               (en la Mac no): si pasa, se borra el original y se reintenta. */
            std::error_code ec;
            std::filesystem::rename(tmp, ruta, ec);
            if (ec) {
                std::error_code ec2;
                std::filesystem::remove(ruta, ec2);
                ec.clear();
                std::filesystem::rename(tmp, ruta, ec);
            }
            if (ec) {
                std::error_code ec3;
                std::filesystem::remove(tmp, ec3);
            }
            return !ec;
        }

        std::string g_idiomaApp;

        std::string localeConsola()
        {
            if (!g_idiomaApp.empty()) return g_idiomaApp;
#ifdef __SWITCH__
            return brls::i18n::getCurrentLocale();
#else
            return "en-US";
#endif
        }

        void anotar(const char* mensaje, int valor)
        {
#ifdef __SWITCH__
            brls::Logger::info("hekate: {} (oqblang={})", mensaje, valor);
#else
            (void)mensaje;
            (void)valor;
#endif
        }

        // Ajusta nyx.ini al idioma de la consola. Falso si no hay nyx.ini.
        bool aplicar(bool soloSiEsDelPack)
        {
            if (!std::filesystem::exists(NYX_INI)) return false;
            const std::string ini = leerArchivo(NYX_INI);
            const int actual = leer(ini);
            const int deseado = desdeLocale(localeConsola());
            if (soloSiEsDelPack && actual != INGLES && actual != -1) {
                anotar("se deja el elegido a mano", actual);
                return true;
            }
            if (actual == deseado) return true;
            const std::string nuevo = conIdioma(ini, deseado);
            if (nuevo == ini) return true;  // sin [config]: no hay donde ponerlo
            if (!escribirArchivo(NYX_INI, nuevo)) {
                anotar("no se pudo escribir nyx.ini", deseado);
                return false;  // sin la marca, se reintenta la próxima vez
            }
            anotar("idioma de la app", deseado);
            return true;
        }
    }  // namespace

    int desdeLocale(const std::string& locale)
    {
        std::string l;
        for (char c : locale) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (l.rfind("es", 0) == 0) return 0;
        if (l.rfind("pt", 0) == 0) return 2;
        return INGLES;
    }

    int leer(const std::string& ini)
    {
        std::istringstream ss(ini);
        std::string linea;
        bool enConfig = false;
        while (std::getline(ss, linea)) {
            if (!linea.empty() && linea.back() == '\r') linea.pop_back();
            if (!linea.empty() && linea[0] == '[') {
                enConfig = linea == "[config]";
                continue;
            }
            if (enConfig && linea.rfind("oqblang=", 0) == 0) {
                const std::string v = linea.substr(8);
                return v.empty() ? -1 : std::atoi(v.c_str());
            }
        }
        return -1;
    }

    std::string conIdioma(const std::string& ini, int valor)
    {
        const bool crlf = ini.find("\r\n") != std::string::npos;
        const std::string fin = crlf ? "\r\n" : "\n";
        const std::string clave = "oqblang=" + std::to_string(valor);

        std::string salida;
        size_t pos = 0;
        bool enConfig = false, puesta = false, hayConfig = false;
        while (pos < ini.size()) {
            size_t nl = ini.find('\n', pos);
            const size_t finLinea = nl == std::string::npos ? ini.size() : nl + 1;
            std::string linea = ini.substr(pos, finLinea - pos);
            std::string limpia = linea;
            while (!limpia.empty() && (limpia.back() == '\n' || limpia.back() == '\r')) limpia.pop_back();

            if (!limpia.empty() && limpia[0] == '[') {
                // Si [config] terminó sin la clave, va al final de esa sección.
                if (enConfig && !puesta) {
                    salida += clave + fin;
                    puesta = true;
                }
                enConfig = limpia == "[config]";
                hayConfig = hayConfig || enConfig;
            }
            else if (enConfig && !puesta && limpia.rfind("oqblang=", 0) == 0) {
                linea = clave + linea.substr(limpia.size());  // conserva el fin de línea
                puesta = true;
            }
            salida += linea;
            pos = finLinea;
        }
        if (enConfig && !puesta) {
            if (!salida.empty() && salida.back() != '\n') salida += fin;
            salida += clave + fin;
            puesta = true;
        }
        return hayConfig ? salida : ini;
    }

    void fijarIdiomaApp(const std::string& idioma)
    {
        g_idiomaApp = idioma;
    }

    void trasInstalarPack()
    {
        if (aplicar(false)) escribirArchivo(HECHO, "1\n");
    }

    void alAbrir()
    {
        if (std::filesystem::exists(HECHO)) return;
        if (aplicar(true)) escribirArchivo(HECHO, "1\n");
    }

}  // namespace hekateIdioma
