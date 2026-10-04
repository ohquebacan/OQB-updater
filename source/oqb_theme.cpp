#include "oqb_theme.hpp"

namespace oqb {

    namespace {

        /* Los dos extremos del degradado del logo. El resto de los verdes salen
           de aquí, no de elegir colores sueltos que se parezcan. */
        constexpr unsigned char BRAND_GREEN[3] = {0x9D, 0xEA, 0x9A};
        constexpr unsigned char BRAND_LIME[3] = {0xCE, 0xF7, 0x81};

        /* En el tema claro el verde del logo no se lee: es un verde pastel
           sobre un fondo casi blanco. Se usa una versión oscurecida del mismo
           tono, que es lo que mantiene la marca sin perder el contraste. */
        constexpr unsigned char BRAND_GREEN_DARK[3] = {0x2F, 0x6E, 0x28};
        constexpr unsigned char BRAND_GREEN_MID[3] = {0x56, 0xA8, 0x4A};
        constexpr unsigned char BRAND_LIME_MID[3] = {0xB0, 0xE2, 0x80};

        NVGcolor rgb(const unsigned char c[3])
        {
            return nvgRGB(c[0], c[1], c[2]);
        }

        class OqbDarkTheme : public brls::HorizonDarkTheme
        {
        public:
            OqbDarkTheme()
            {
                /* El fondo son dos campos y hay que tocar los dos: el de float
                   es el que usa el GL para limpiar la pantalla, y el NVGcolor
                   el que pintan las vistas. Cambiar solo uno deja el fondo real
                   como estaba. */
                this->backgroundColor[0] = 11.0f / 255.0f;
                this->backgroundColor[1] = 15.0f / 255.0f;
                this->backgroundColor[2] = 10.0f / 255.0f;
                this->backgroundColorRGB = nvgRGB(11, 15, 10);

                this->sidebarColor = nvgRGB(18, 23, 17);
                this->sidebarSeparatorColor = nvgRGB(42, 51, 39);
                this->activeTabColor = rgb(BRAND_GREEN);

                /* Los dos highlight son el degradado de la selección al
                   moverse. Poniendo el verde y el lima del logo, el brillo que
                   recorre la lista es el mismo que tiene la marca. */
                this->highlightColor1 = rgb(BRAND_GREEN);
                this->highlightColor2 = rgb(BRAND_LIME);
                this->highlightBackgroundColor = nvgRGB(18, 26, 16);

                this->listItemValueColor = rgb(BRAND_GREEN);
                this->listItemSeparatorColor = nvgRGB(36, 43, 34);

                // La barrita que precede a cada cabecera de sección.
                this->headerRectangleColor = rgb(BRAND_GREEN);

                this->buttonPrimaryEnabledBackgroundColor = rgb(BRAND_LIME);
                // Texto oscuro: el lima es claro y el blanco encima no se lee.
                this->buttonPrimaryEnabledTextColor = nvgRGB(10, 20, 8);

                this->dialogButtonColor = rgb(BRAND_GREEN);
            }
        };

        class OqbLightTheme : public brls::HorizonLightTheme
        {
        public:
            OqbLightTheme()
            {
                // Un blanco con una pizca de verde, no un gris neutro: así el
                // fondo acompaña al acento en vez de pelearse con él.
                this->backgroundColor[0] = 237.0f / 255.0f;
                this->backgroundColor[1] = 240.0f / 255.0f;
                this->backgroundColor[2] = 235.0f / 255.0f;
                this->backgroundColorRGB = nvgRGB(237, 240, 235);

                this->sidebarColor = nvgRGB(242, 245, 240);
                this->activeTabColor = rgb(BRAND_GREEN_DARK);

                this->highlightColor1 = rgb(BRAND_GREEN_MID);
                this->highlightColor2 = rgb(BRAND_LIME_MID);

                this->listItemValueColor = rgb(BRAND_GREEN_DARK);
                this->headerRectangleColor = rgb(BRAND_GREEN_DARK);

                this->buttonPrimaryEnabledBackgroundColor = rgb(BRAND_GREEN_DARK);
                this->buttonPrimaryEnabledTextColor = nvgRGB(255, 255, 255);

                this->dialogButtonColor = rgb(BRAND_GREEN_DARK);
            }
        };

    }  // namespace

    brls::LibraryViewsThemeVariantsWrapper* themeVariants()
    {
        // Lo libera borealis al cerrar: el wrapper borra las dos variantes.
        return new brls::LibraryViewsThemeVariantsWrapper(new OqbLightTheme(), new OqbDarkTheme());
    }

}  // namespace oqb
