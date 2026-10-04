#pragma once

#include <borealis.hpp>
#include <json.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "constants.hpp"

class ListDownloadTab : public brls::List
{
private:
    brls::ListItem* listItem;
    nlohmann::ordered_json nxlinks;
    std::string currentCheatsVer = "";
    std::string newCheatsVer = "";
    contentType type;
    // Clave del nx-links.json de la que sale la lista. Vacia = la del tipo.
    // Permite partir las apps en secciones sin tocar la logica de descarga ni
    // la de extraccion, que siguen tratandolo todo como contentType::apps.
    std::string jsonKey;
    // Texto por el que se filtra la lista. Vacio = la lista entera. Filtrar no
    // cambia nada de la descarga: el filtro solo decide que entradas se
    // construyen, asi que una entrada encontrada por el buscador se baja por el
    // mismo camino que la de la lista completa.
    std::string filter;
    void createList();
    void createList(contentType type);
    void createCheatSlipItem();
    void createGbatempItem();
    void createGfxItem();
    void setDescription();
    void setDescription(contentType type);
    void displayNotFound();
    // La entrada "Buscar" al principio de la lista. Abre el teclado y muestra
    // los resultados en otra pagina, en vez de reconstruir esta: borrar vistas
    // mientras el foco esta en una de ellas es como se cuelga borealis.
    void createSearchItem();
    // Las entradas que pasan el filtro, en el orden del json.
    std::vector<std::pair<std::string, std::string>> applyFilter(
        const std::vector<std::pair<std::string, std::string>>& links) const;
    // Añade la pregunta del acceso directo, el worker que lo crea y la página
    // de resultado. nroPath puede llenarse recién durante los stages previos.
    void addForwarderStages(brls::StagedAppletFrame* stagedFrame, std::shared_ptr<std::string> nroPath);

public:
    // Pide el texto y abre los resultados. Es publica porque el menu de Apps
    // tambien la ofrece: ahi se busca en todo el catalogo sin tener que entrar
    // antes a una seccion, que es como se busca algo cuando no se sabe en cual
    // esta. jsonKey vacia = todo el catalogo del tipo.
    static void openSearch(contentType type, const nlohmann::ordered_json& nxlinks, const std::string& jsonKey = "");

    ListDownloadTab(const contentType type, const nlohmann::ordered_json& nxlinks = nlohmann::ordered_json::object(), const std::string& jsonKey = "", const std::string& filter = "");
};