#pragma once

#include <json.hpp>
#include <string>

// Instala o actualiza un .nro del catalogo con el mismo flujo que la pestana de
// apps: confirmar, descargar donde ya este (o en su carpeta), anotar la fecha y
// ofrecer el acceso directo del menu HOME. Acepta .nro y zip.
void instalarAppDelCatalogo(const nlohmann::ordered_json& nxlinks, const std::string& nombre, const std::string& url);

/** La app del catalogo segun como este: sin instalar o con version nueva, la
    instala o actualiza; al dia, la abre (con esos argumentos). */
void abrirOInstalar(const nlohmann::ordered_json& nxlinks, const std::string& nombre, const std::string& url, const std::string& argumentos = "");
