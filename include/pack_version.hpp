#pragma once

#include <json.hpp>
#include <string>

// La fecha de cada pack, para poder avisar de que hay uno más nuevo.
//
// El tag de un release no cambia cuando se vuelve a subir el zip: el pack V1.4
// se publicó el 27 de septiembre y su archivo se reemplazó el 2 de octubre.
// Quien lo descargó en medio tiene un pack distinto y no tenía forma de saberlo.
//
// El repositorio del pack publica pack-versions.json con la fecha de subida real
// de cada uno. Aquí se compara con lo que el usuario instaló, que se guarda en
// la SD al terminar la instalación.
namespace packVersion {

    struct Info
    {
        std::string date;  // AAAA-MM-DD, vacía si el pack no está publicado
        std::string note;  // texto opcional que escribe quien publica el pack
    };

    // Descarga el json publicado. Devuelve un objeto vacío si no se pudo: sin
    // conexión no se avisa de nada, que es mejor que avisar en falso.
    nlohmann::ordered_json fetchPublished();

    // La clave de un pack es el nombre de su archivo, así que sale del propio
    // enlace y no hay que mantener una lista aparte que se desincronice.
    std::string keyFromUrl(const std::string& url);

    Info published(const nlohmann::ordered_json& all, const std::string& key);

    // Lo que el usuario tiene instalado, o vacío si nunca instaló desde la app.
    std::string installed(const std::string& key);

    // Se llama al terminar de extraer. Guarda la fecha que estaba publicada en
    // ese momento, no la de hoy: es la del pack que acaba de quedar en la SD.
    void recordInstalled(const std::string& key, const std::string& date);

}  // namespace packVersion
