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
    // Las entradas que pasan el filtro, en el orden del json.
    std::vector<std::pair<std::string, std::string>> applyFilter(
        const std::vector<std::pair<std::string, std::string>>& links) const;
    // Añade la pregunta del acceso directo, el worker que lo crea y la página
    // de resultado. nroPath puede llenarse recién durante los stages previos.
    void addForwarderStages(brls::StagedAppletFrame* stagedFrame, std::shared_ptr<std::string> nroPath);

public:
    /* Pide el texto y abre los resultados.
       La ofrece solo el menu de categorias de Apps, que es donde sirve: ahi se
       busca en todo el catalogo sin tener que acertar antes la categoria. Dentro
       de una categoria no aparece — son pocas entradas y se ven de un vistazo —
       ni en las demas listas.
       jsonKey vacia = todo el catalogo del tipo. */
    static void openSearch(contentType type, const nlohmann::ordered_json& nxlinks, const std::string& jsonKey = "");

    ListDownloadTab(const contentType type, const nlohmann::ordered_json& nxlinks = nlohmann::ordered_json::object(), const std::string& jsonKey = "", const std::string& filter = "");
};