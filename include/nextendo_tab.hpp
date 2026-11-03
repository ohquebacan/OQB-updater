#pragma once

#include <borealis.hpp>
#include <json.hpp>

// Seccion "Nextendo": sale solo con la consola en modo Nextendo. Un checklist
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

    void addChecklist(const nlohmann::ordered_json& datos);
    void addSearch(const nlohmann::ordered_json& datos);

public:
    explicit NextendoTab(const nlohmann::ordered_json& nxlinks);

    // Si el catalogo trae la seccion (un pack viejo no la tiene).
    static bool available(const nlohmann::ordered_json& nxlinks);
};
