#pragma once

#include <borealis.hpp>
#include <json.hpp>
#include <string>
#include <utility>
#include <vector>

// La pestaña de Apps ya no lista las aplicaciones directamente: muestra las
// secciones y cada una abre su propia lista. Asi se puede dividir el catalogo
// sin que el menu principal crezca con una pestaña por categoria.
class AppsTab : public brls::List
{
public:
    AppsTab(const nlohmann::ordered_json& nxlinks);
    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;

private:
    nlohmann::ordered_json nxlinks;
    // Cada seccion con su clave, para recontar sus versiones nuevas.
    std::vector<std::pair<brls::ListItem*, std::string>> secciones;
    int genInstalaciones = 0;
};
