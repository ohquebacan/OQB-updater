#include "crash_report.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace crash {

    namespace {

        std::string trim(const std::string& s)
        {
            const auto b = s.find_first_not_of(" \t\r\n");
            if (b == std::string::npos) return "";
            const auto e = s.find_last_not_of(" \t\r\n");
            return s.substr(b, e - b + 1);
        }

        std::string lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s;
        }

        // Valor de la primera linea "Clave:   valor" del informe.
        std::string field(const std::string& contents, const std::string& key)
        {
            std::istringstream ss(contents);
            std::string line;
            while (std::getline(ss, line)) {
                const std::string t = trim(line);
                if (t.rfind(key + ":", 0) == 0)
                    return trim(t.substr(key.size() + 1));
            }
            return "";
        }

        bool hasExtension(const std::string& name, const char* ext)
        {
            const std::string e(ext);
            return name.size() > e.size() && lower(name.substr(name.size() - e.size())) == e;
        }

    }  // namespace

    bool parse(const std::string& file, const std::string& contents, Report& out)
    {
        out = Report{};
        out.file = file;

        out.programId = lower(field(contents, "Program ID"));
        if (out.programId.size() != 16) return false;

        out.processName = field(contents, "Process Name");

        // "Result:  0x4A8 (2168-0002)" -> "2168-0002"
        const std::string result = field(contents, "Result");
        const auto abre = result.find('(');
        const auto cierra = result.find(')', abre == std::string::npos ? 0 : abre);
        if (abre != std::string::npos && cierra != std::string::npos)
            out.result = result.substr(abre + 1, cierra - abre - 1);

        // "PC:  000000003feeca74 (tinfoil + 0x2eca74)" -> "tinfoil". Los
        // modulos sin nombre salen como "[f35b0f5b]": esos no dicen nada.
        const std::string pc = field(contents, "PC");
        const auto p = pc.find('(');
        if (p != std::string::npos) {
            std::string mod = pc.substr(p + 1);
            const auto fin = mod.find_first_of(" +)");
            mod = trim(mod.substr(0, fin));
            if (!mod.empty() && mod[0] != '[') out.module = mod;
        }

        // El nombre empieza con los segundos desde 1970: "01792357843_<id>.log".
        const auto guion = file.find('_');
        if (guion != std::string::npos) {
            const std::string num = file.substr(0, guion);
            if (!num.empty() && std::all_of(num.begin(), num.end(), [](unsigned char c) { return std::isdigit(c); }))
                out.timestamp = std::stoull(num);
        }
        return true;
    }

    std::vector<Report> readAll(const std::string& dir)
    {
        std::vector<Report> res;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            const std::string name = entry.path().filename().string();
            if (!hasExtension(name, ".log")) continue;

            std::ifstream f(entry.path().string());
            std::stringstream contenido;
            contenido << f.rdbuf();

            Report r;
            if (parse(name, contenido.str(), r)) res.push_back(r);
        }
        std::sort(res.begin(), res.end(), [](const Report& a, const Report& b) { return a.timestamp > b.timestamp; });
        return res;
    }

    std::vector<Report> readAll(const std::vector<std::string>& dirs)
    {
        std::vector<Report> res;
        for (const auto& dir : dirs) {
            auto parte = readAll(dir);
            res.insert(res.end(), parte.begin(), parte.end());
        }
        std::sort(res.begin(), res.end(), [](const Report& a, const Report& b) { return a.timestamp > b.timestamp; });
        return res;
    }

    std::vector<Group> group(const std::vector<Report>& reports)
    {
        std::map<std::string, Group> porPrograma;
        for (const auto& r : reports) {
            Group& g = porPrograma[r.programId];
            g.programId = r.programId;
            ++g.count;
            if (r.timestamp >= g.lastTimestamp) {
                g.lastTimestamp = r.timestamp;
                g.processName = r.processName;
                g.module = r.module;
                g.lastResult = r.result;
            }
        }

        std::vector<Group> res;
        for (auto& [id, g] : porPrograma) res.push_back(g);
        std::sort(res.begin(), res.end(), [](const Group& a, const Group& b) { return a.lastTimestamp > b.lastTimestamp; });
        return res;
    }

    /* Las fallas de hardware no tienen informe propio: aparecen cuando un programa
       cae con un codigo del modulo que maneja esa pieza. El codigo se muestra como
       2000 + modulo (switchbrew, "Error codes"), asi que basta la primera mitad.
       Solo van modulos cuya relacion con una pieza esta documentada; el resto se
       queda sin causa antes que adivinar. */
    static std::string hardwareHint(const std::string& result)
    {
        if (result.size() < 9 || result[4] != '-') return "";
        const std::string modulo = result.substr(0, 4);
        const std::string repite = " Si se repite aunque reinicies y sin homebrew abierto, es muy probable que la falla sea de hardware y no de los archivos de la SD, así que reinstalar el pack no lo arreglará: conviene llevarla a revisión.";
        if (modulo == "2101")
            return "Error de comunicación con un chip interno (bus I2C). Si se repite, es probable que sea el chip de carga "
                   "o de energía (M92T36), sobre todo si además la consola no carga bien." + repite;
        if (modulo == "2136" || modulo == "2133")
            return "Falló la gestión de energía o de la batería. Puede ser el cargador o el cable; si con otro cargador "
                   "oficial sigue igual, es probable que sea la batería o el chip de carga." + repite;
        if (modulo == "2153")
            return "Falló el audio. Si los parlantes suenan mal o no suenan, es probable que sea un parlante en corto, "
                   "el conector de audífonos o el chip de audio." + repite;
        if (modulo == "2107")
            return "Falló el Wi-Fi. Si la consola no detecta ninguna red y los demás equipos sí, es probable que sea el "
                   "chip o las antenas de Wi-Fi." + repite;
        if (modulo == "2113")
            return "Falló el Bluetooth. Si los controles no se conectan sin cable, puede ser el chip de Wi-Fi y Bluetooth "
                   "(es el mismo) o sus antenas." + repite;
        if (modulo == "2139")
            return "Falló el control de temperatura. Si la consola se calienta mucho o el ventilador no suena, es probable "
                   "que sea el ventilador o la pasta térmica." + repite;
        if (modulo == "2024")
            return "Error al leer la tarjeta SD o la memoria interna. Lo más probable es la tarjeta SD: pruébala en la PC "
                   "o cambia a otra.";
        return "";
    }

    std::string hint(const Group& g)
    {
        /* Primero el hardware: si el codigo es de una pieza, el programa que cayo
           (menu HOME incluido) solo es el que se entero. */
        /* Solo para programas del sistema: un sysmodule de homebrew que cae con un
           codigo de energia (sys-clk, por ejemplo) es mas probable que sea el propio
           sysmodule. Mismo criterio que la pantalla de error de Atmosphere de OQB. */
        const bool delSistema = g.programId.size() == 16 && g.programId.compare(0, 12, "010000000000") == 0;
        const std::string hw = delSistema ? hardwareHint(g.lastResult) : "";
        if (!hw.empty()) return hw;

        const std::string proceso = lower(g.processName);
        const std::string modulo = lower(g.module);

        /* El informe dice que programa cayo y con que codigo, no por que: la causa se
           da como probable y con la condicion que la hace probable. */
        if (g.programId == "0100000000001000")
            return "Falló el menú HOME. Lo más probable es un tema o un menú alternativo (HomeNX, Switch U). Para quitarlo, "
                   "en Hekate toca «More Configs» › «LIMPIAR TEMA», elige «Limpiar tema» con POWER y confirma con VOL+. "
                   "Si no tenías ninguno, la causa es otra: verifica que el HATS PACK esté instalado correctamente y actualizado.";
        if (proceso == "ovlloader")
            return "Falló un overlay de Ultrahand" + (g.module.empty() ? std::string(".") : " (" + g.module + ").") +
                   " Lo más probable es que no funcione con esta versión: actualízalo o quítalo de switch/.overlays.";
        if (g.programId == "010000000000100d" || proceso == "hbloader")
            return "Falló una app abierta desde el homebrew menu. Si es una de las que trae el pack, lo más probable es que hayan "
                   "quedado archivos viejos mezclados al copiar a mano: vuelve a instalar el pack desde aquí o desde la PC.";
        if (g.programId.rfind("05", 0) == 0)
            return "Falló un acceso directo (forwarder). Puede ser el acceso directo o la app a la que lleva. "
                   "Si se repite, vuelve a crearlo desde Herramientas; si la app es del pack, vuelve a instalar el pack.";
        if (modulo == "tinfoil")
            return "Falló Tinfoil. Si se repite al abrirlo, lo más probable es que hayan quedado archivos viejos mezclados: "
                   "vuelve a instalar el pack desde aquí o desde la PC.";
        return "";
    }

    std::string date(uint64_t timestamp)
    {
        if (timestamp == 0) return "?";
        const std::time_t t = static_cast<std::time_t>(timestamp);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[16];
        std::strftime(buf, sizeof(buf), "%d/%m/%Y", &tm);
        return buf;
    }

    uint64_t totalSize(const std::string& dir)
    {
        uint64_t total = 0;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            const std::string name = entry.path().filename().string();
            if (!hasExtension(name, ".log") && !hasExtension(name, ".jpg")) continue;
            std::error_code sizeEc;
            const auto size = std::filesystem::file_size(entry.path(), sizeEc);
            if (!sizeEc) total += size;
        }
        return total;
    }

    int removeAll(const std::string& dir)
    {
        std::vector<std::string> borrar;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            const std::string name = entry.path().filename().string();
            if (hasExtension(name, ".log") || hasExtension(name, ".jpg"))
                borrar.push_back(entry.path().string());
        }

        // Se junta primero y se borra despues: borrar mientras se recorre la
        // carpeta salta entradas en algunos sistemas de archivos.
        int borrados = 0;
        for (const auto& p : borrar) {
            std::error_code rmEc;
            if (std::filesystem::remove(p, rmEc) && !rmEc) ++borrados;
        }
        return borrados;
    }

}  // namespace crash
