#pragma once

#include <borealis.hpp>
#include <json.hpp>
//#include "ntcp.hpp"

class ToolsTab : public brls::List
{
private:
    brls::StagedAppletFrame* stagedFrame;

public:
    // nxlinks: el catalogo, para la lista de packs de la instalacion limpia.
    ToolsTab(const std::string& tag, const nlohmann::ordered_json& payloads, bool erista = true, const nlohmann::ordered_json& hideStatus = {},
             const nlohmann::ordered_json& nxlinks = nlohmann::ordered_json::object());
};