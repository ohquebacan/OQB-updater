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

    /* Colores dibujados a mano (no los del tema de borealis). Cada uno tiene su
       version para el modo oscuro y para el claro: un texto claro pensado para
       el fondo oscuro desaparece sobre el gris claro (paso con el registro de
       descargas, 8-nov-2026). Usar estos en vez de nvgRGB fijos. */
    inline bool oscuro()
    {
        return brls::Application::getThemeVariant() == brls::ThemeVariant::DARK;
    }
    // Texto de estado sobre su fondo tenue (pildoras, cajas de aviso).
    inline NVGcolor verdeTexto() { return oscuro() ? nvgRGB(157, 234, 154) : nvgRGB(20, 120, 70); }
    inline NVGcolor doradoTexto() { return oscuro() ? nvgRGB(230, 185, 92) : nvgRGB(140, 90, 0); }
    inline NVGcolor rojoTexto() { return oscuro() ? nvgRGB(240, 149, 149) : nvgRGB(175, 40, 40); }
    inline NVGcolor grisTexto() { return oscuro() ? nvgRGB(160, 160, 160) : nvgRGB(95, 95, 95); }
    /* Temas de color (Herramientas > Tema), con la misma paleta que JKSV BACAN:
       cambian fondo, panel, lineas y el acento. Los colores de estado (verde al
       dia, dorado aviso, rojo falla) no cambian: significan lo mismo en todos. */
    int cantidadTemas();
    const char* nombreTema(int indice);
    int temaActual();
    // Lo aplica al instante (los dos temas de borealis y los acentos) y lo guarda.
    void elegirTema(int indice);
    // El acento del tema: el claro (texto, seleccion) y el fuerte (botones, barras).
    NVGcolor acentoClaro();
    NVGcolor acentoFuerte();
    // El acento tal cual, sin ajustar al modo claro (la pantalla de carga es negra).
    NVGcolor acentoBase(bool fuerte);

    // El acento de la marca (puntos, barras, botones): sigue al tema.
    inline NVGcolor esmeralda() { return acentoFuerte(); }
    // Un velo neutro para separar una zona del fondo (blanco en oscuro, negro en claro).
    inline NVGcolor velo(unsigned char alfa) { return oscuro() ? nvgRGBA(255, 255, 255, alfa) : nvgRGBA(0, 0, 0, alfa); }

}  // namespace oqb
