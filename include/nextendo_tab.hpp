#pragma once

#include <borealis.hpp>
#include <json.hpp>

// Seccion "Nextendo": sale siempre, para despertar curiosidad. Sin el modo
// activo empieza explicando que es y como empezar. Un checklist
// (lo que la app puede revisar sola y lo que hay que confirmar a mano), que
// hacer segun donde fallo, los codigos de error con un buscador y las notas.
//
// Todo el texto viene del catalogo (clave "nextendo" de nx-links.json), sacado
// de la wiki de Nextendo y adaptado a la consola: si cambia algo, se actualiza
// sin sacar una version de la app.
class NextendoTab : public brls::List
{
private:
    nlohmann::ordered_json nxlinks;
    bool activo;

    void addChecklist(const nlohmann::ordered_json& datos);
    void addSearch(const nlohmann::ordered_json& datos);

public:
    // `activo`: la consola esta en modo Nextendo (lo dice el verificador).
    NextendoTab(const nlohmann::ordered_json& nxlinks, bool activo);

    // Si el catalogo trae la seccion (un pack viejo no la tiene).
    static bool available(const nlohmann::ordered_json& nxlinks);

    /* Si ya se abrio alguna vez. Hasta entonces el menu la marca con un punto;
       despues no insiste. */
    static bool seen();

    /* Si Nextendo esta funcionando: modo activo, firmware compatible y Prelude
       instalado y al dia (los checks principales). El menu lo marca con el punto
       encendido, para saber de un vistazo que esta activo. */
    static bool working(const nlohmann::ordered_json& nxlinks, bool activo);

    void willAppear(bool resetState = false) override;
};
