#include "pack_version.hpp"

#include <fstream>

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
        fs::writeJsonToFile(registro, PACK_VERSIONS_PATH);
    }

}  // namespace packVersion
