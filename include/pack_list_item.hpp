#pragma once

#include <borealis.hpp>

#include <string>

/* Un item de lista que puede avisar de que lo instalado quedo viejo.
 *
 * El texto solo no alcanza: "Hay actualizacion" al lado derecho se lee igual
 * que "Actualizado el 2026-10-02" y queda en el mismo gris. Marcado, el ojo lo
 * encuentra sin leer toda la lista.
 *
 * El pulso se calcula con un reloj, no con una animacion de borealis: una
 * animacion registrada sobre un miembro sigue escribiendo despues de que la
 * vista se destruyo, que es exactamente el crash que tenemos pendiente en
 * ~ScrollView. Aqui no hay nada que cancelar.
 */
class PackListItem : public brls::ListItem
{
public:
    PackListItem(const std::string& label, const std::string& description = "", const std::string& subLabel = "");

    // El valor que se muestra a la derecha, marcado como aviso.
    void setWarningValue(const std::string& value);

    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;

private:
    bool warning = false;
};
