#include "pack_version.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

#include "constants.hpp"
#include "download.hpp"
#include "fs.hpp"

namespace packVersion {

    nlohmann::ordered_json fetchPublished()
    {
        nlohmann::ordered_json res;
        download::getRequest(PACK_VERSIONS_URL, res);
        return res.is_object() ? res : nlohmann::ordered_json::object();
    }

    std::string keyFromUrl(const std::string& url)
    {
        const size_t barra = url.rfind('/');
        return barra == std::string::npos ? url : url.substr(barra + 1);
    }

    bool couldBeTracked(const std::string& url)
    {
        /* De "https://raw.githubusercontent.com/dueno/repo/main/..." se saca
           "/dueno/repo/", que es lo que llevan también los enlaces de descarga
           de los releases de ese repositorio. */
        const std::string fuente = PACK_VERSIONS_URL;

        const size_t host = fuente.find("://");
        if (host == std::string::npos)
            return false;

        const size_t duenoIni = fuente.find('/', host + 3);
        if (duenoIni == std::string::npos)
            return false;

        const size_t repoIni = fuente.find('/', duenoIni + 1);
        if (repoIni == std::string::npos)
            return false;

        const size_t repoFin = fuente.find('/', repoIni + 1);
        if (repoFin == std::string::npos)
            return false;

        return url.find(fuente.substr(duenoIni, repoFin - duenoIni + 1)) != std::string::npos;
    }

    Info published(const nlohmann::ordered_json& all, const std::string& key)
    {
        Info info;
        if (!all.is_object() || key.empty())
            return info;

        const auto entrada = all.find(key);
        if (entrada == all.end() || !entrada->is_object())
            return info;

        const auto fecha = entrada->find("fecha");
        if (fecha != entrada->end() && fecha->is_string())
            info.date = fecha->get<std::string>();

        const auto nota = entrada->find("nota");
        if (nota != entrada->end() && nota->is_string())
            info.note = nota->get<std::string>();

        return info;
    }

    std::string installed(const std::string& key)
    {
        const nlohmann::ordered_json registro = fs::parseJsonFile(PACK_VERSIONS_PATH);
        if (!registro.is_object())
            return "";

        const auto entrada = registro.find(key);
        if (entrada == registro.end() || !entrada->is_string())
            return "";

        return entrada->get<std::string>();
    }

    namespace {
        // Clave del registro con el pack que quedo en la SD. Empieza con "_"
        // para que nunca coincida con el nombre de un zip; las versiones viejas
        // de la app la ignoran porque solo buscan su propio pack.
        constexpr const char CURRENT_KEY[] = "_actual";

        std::string lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s;
        }
    }  // namespace

    std::string current()
    {
        const nlohmann::ordered_json registro = fs::parseJsonFile(PACK_VERSIONS_PATH);
        if (!registro.is_object())
            return "";

        const auto actual = registro.find(CURRENT_KEY);
        if (actual != registro.end() && actual->is_string())
            return actual->get<std::string>();

        /* Instalado con una version que no anotaba cual era. Con un solo pack
           registrado no hay duda. Con varios se mira hekate_ipl.ini: el pack de
           overclock es el unico que carga hoc.kip, y la instalacion reescribe
           ese ini, asi que dice cual de los dos se instalo al final. */
        std::vector<std::string> packs;
        for (const auto& [clave, valor] : registro.items()) {
            if (!clave.empty() && clave[0] != '_' && valor.is_string())
                packs.push_back(clave);
        }
        if (packs.size() == 1)
            return packs[0];
        if (packs.empty())
            return "";

        std::ifstream ini(HEKATE_IPL_PATH);
        std::stringstream contenido;
        contenido << ini.rdbuf();
        const bool overclock = lower(contenido.str()).find("kips/hoc.kip") != std::string::npos;

        std::string elegido;
        for (const auto& clave : packs) {
            const bool esOverclock = lower(clave).find("-oc-") != std::string::npos;
            if (esOverclock == overclock) {
                if (!elegido.empty())
                    return "";  // mas de uno encaja: mejor no afirmar nada
                elegido = clave;
            }
        }
        return elegido;
    }

    void recordInstalled(const std::string& key, const std::string& date)
    {
        if (key.empty() || date.empty())
            return;

        /* Se lee lo que ya había y se cambia solo esta clave: hay dos packs y
           instalar uno no puede borrar lo que se sabe del otro. */
        nlohmann::ordered_json registro = fs::parseJsonFile(PACK_VERSIONS_PATH);
        if (!registro.is_object())
            registro = nlohmann::ordered_json::object();

        registro[key] = date;
        // En la SD solo cabe un pack: el que se acaba de instalar es el actual.
        registro[CURRENT_KEY] = key;
        fs::writeJsonToFile(registro, PACK_VERSIONS_PATH);
    }

}  // namespace packVersion
