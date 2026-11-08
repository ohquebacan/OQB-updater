#pragma once

#include <borealis.hpp>
#include <json.hpp>
#include <string>
#include <utility>
#include <vector>

#include "pack_list_item.hpp"

#include "protection.hpp"

// Seccion "Bacán Shop", debajo de Nextendo. Dos estados:
// - Sin instalar: que es la tienda, que trae y un boton para instalarla.
// - Instalada: su version (al dia o con actualizacion, que se instala con A) y
//   "Listo para instalar juegos", lo que hace falta para que lo que se baje de
//   la tienda funcione: firmas, emuNAND, bloqueo de Nintendo y espacio en la SD;
//   y apps que mejoran la experiencia al jugar (mods, FPS).
// En los dos, los trucos: antes eran una pestana aparte.
class BacanShopTab : public brls::List
{
public:
    // `checks`: las protecciones que ya comprobo MainFrame. `conTrucos`: falso
    // si hide_tabs.json oculta los trucos.
    BacanShopTab(const nlohmann::ordered_json& nxlinks, const std::vector<protection::Check>& checks, bool conTrucos);

    // Si el catalogo trae la tienda (en cualquier seccion de apps de nx-links.json).
    static bool available(const nlohmann::ordered_json& nxlinks);
    // La URL del .nro en el catalogo, o vacia.
    static std::string url(const nlohmann::ordered_json& nxlinks);
    // Si hay una base de trucos mas nueva que la descargada (y ya se bajo alguna vez).
    static bool trucosNuevos();
    // Si ya se abrio la seccion alguna vez (sin instalar, el menu la marca hasta entonces).
    static bool seen();

    void willAppear(bool resetState = false) override;
    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;

    // Las filas de apps sugeridas, para marcarlas al dia tras instalar una.
    std::vector<std::pair<PackListItem*, std::string>> sugeridas;

private:
    nlohmann::ordered_json links;
    std::string direccion;
    brls::View* ficha = nullptr;     // la ficha de arriba (su pildora de estado)
    PackListItem* accion = nullptr;  // instalar / actualizar / volver a instalar
    int genInstalaciones = 0;
    void refrescarEstado();
};
