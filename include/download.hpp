#pragma once

constexpr int ON = 1;
constexpr int OFF = 0;

#include <json.hpp>

namespace download {

    long downloadFile(const std::string& url, std::vector<std::uint8_t>& res, const std::string& output = "", int api = OFF);
    long downloadFile(const std::string& url, const std::string& output = "", int api = OFF);
    std::vector<std::pair<std::string, std::string>> getLinks(const std::string& url);
    std::vector<std::pair<std::string, std::string>> getLinksFromJson(const nlohmann::ordered_json& json_object);
    std::string fetchTitle(const std::string& url);
    long downloadPage(const std::string& url, std::string& res, const std::vector<std::string>& headers = {}, const std::string& body = "");
    long getRequest(const std::string& url, nlohmann::ordered_json& res, const std::vector<std::string>& headers = {}, const std::string& body = "");

    // Un texto corto (hasta 64 KB) con un tope total de `segundos`. Su propio
    // curl, sin barra de progreso ni registro: sirve desde cualquier hilo.
    // Devuelve el codigo HTTP (0 si no hubo respuesta completa).
    long textoCorto(const std::string& url, std::string& out, long segundos);

}  // namespace download