#pragma once

#include <json.hpp>
#include <string>

// Instala o actualiza un .nro del catalogo con el mismo flujo que la pestana de
// apps: confirmar, descargar donde ya este (o en su carpeta), anotar la fecha y
// ofrecer el acceso directo del menu HOME. Acepta .nro y zip.
void instalarAppDelCatalogo(const nlohmann::ordered_json& nxlinks, const std::string& nombre, const std::string& url);
