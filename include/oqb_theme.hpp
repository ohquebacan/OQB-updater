#pragma once

#include <borealis.hpp>

// El tema de la app.
//
// Sin esto, borealis usa el suyo — el del menú de la Switch — y toda la
// interfaz sale en cian y verde azulado de Nintendo. Aquí solo se cambian los
// colores que llevan el acento; el resto se hereda, porque el gris de fondo y
// los separadores de Horizon ya están bien calibrados y rehacerlos sería
// romper algo sin motivo.
//
// El verde y el lima están medidos sobre icon.jpg, el logo de la app: no son un
// color elegido, son los que ya tiene la marca.
namespace oqb {

    // Las dos variantes, listas para pasar a brls::Application::init. La
    // consola elige una u otra según su propio ajuste de tema claro/oscuro.
    brls::LibraryViewsThemeVariantsWrapper* themeVariants();

    // Las medidas. Solo cambian las de la barra lateral, que con siete pestañas
    // no caben en la altura de la pantalla. Ver el cálculo en oqb_theme.cpp.
    brls::Style* style();

}  // namespace oqb
