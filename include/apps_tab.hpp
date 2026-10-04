#pragma once

#include <borealis.hpp>
#include <json.hpp>

// La pestaña de Apps ya no lista las aplicaciones directamente: muestra las
// secciones y cada una abre su propia lista. Asi se puede dividir el catalogo
// sin que el menu principal crezca con una pestaña por categoria.
class AppsTab : public brls::List
{
public:
    AppsTab(const nlohmann::ordered_json& nxlinks);

private:
    nlohmann::ordered_json nxlinks;
};
