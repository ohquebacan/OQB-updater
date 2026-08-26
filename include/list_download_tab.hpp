#pragma once

#include <borealis.hpp>
#include <json.hpp>
#include <memory>
#include <string>

#include "constants.hpp"

class ListDownloadTab : public brls::List
{
private:
    brls::ListItem* listItem;
    nlohmann::ordered_json nxlinks;
    std::string currentCheatsVer = "";
    std::string newCheatsVer = "";
    contentType type;
    void createList();
    void createList(contentType type);
    void createCheatSlipItem();
    void createGbatempItem();
    void createGfxItem();
    void setDescription();
    void setDescription(contentType type);
    void displayNotFound();
    // Añade la pregunta del acceso directo, el worker que lo crea y la página
    // de resultado. nroPath puede llenarse recién durante los stages previos.
    void addForwarderStages(brls::StagedAppletFrame* stagedFrame, std::shared_ptr<std::string> nroPath);

public:
    ListDownloadTab(const contentType type, const nlohmann::ordered_json& nxlinks = nlohmann::ordered_json::object());
};