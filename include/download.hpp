#pragma once

constexpr int ON = 1;
constexpr int OFF = 0;

#include <curl/curl.h>
#include <cstdint>
#include <functional>
#include <json.hpp>

namespace download {

    // Toda conexion de curl abre y cierra su socket con el candado comun (5.0.0).
    void protegerSockets(CURL* curl);

    long downloadFile(const std::string& url, std::vector<std::uint8_t>& res, const std::string& output = "", int api = OFF);
    long downloadFile(const std::string& url, const std::string& output = "", int api = OFF);
    std::vector<std::pair<std::string, std::string>> getLinks(const std::string& url);
    std::vector<std::pair<std::string, std::string>> getLinksFromJson(const nlohmann::ordered_json& json_object);
    std::string fetchTitle(const std::string& url);
    long downloadPage(const std::string& url, std::string& res, const std::vector<std::string>& headers = {}, const std::string& body = "");
    long getRequest(const std::string& url, nlohmann::ordered_json& res, const std::vector<std::string>& headers = {}, const std::string& body = "");

    // Un texto corto (hasta `maximo`, 64 KB si no se dice) con un tope total de `segundos`. Su propio
    // curl, sin barra de progreso ni registro: sirve desde cualquier hilo.
    // Devuelve el codigo HTTP (0 si no hubo respuesta completa).
    long textoCorto(const std::string& url, std::string& out, long segundos, size_t maximo = 64u << 10);

    /* Un trozo de un archivo remoto (bytes desde..hasta, inclusive). `total`
       recibe el tamano del archivo entero (de Content-Range). Devuelve el
       codigo HTTP: 206 si el servidor dio el trozo. Sin barra de progreso. */
    long rango(const std::string& url, std::uint64_t desde, std::uint64_t hasta, std::string& out, std::uint64_t* total = nullptr);

    /* Baja un archivo entregando los bytes a `recibir` a medida que llegan, sin
       guardarlo. Si `recibir` devuelve false se corta. Mueve la barra de
       progreso como downloadFile. Devuelve el codigo HTTP (0 si se corto). */
    long enVivo(const std::string& url, const std::function<bool(const std::uint8_t*, std::size_t)>& recibir);

}  // namespace download