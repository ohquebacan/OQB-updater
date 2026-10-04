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

    namespace {

        /* La barra lateral no cabe, y no es cosa de los separadores.

           Borealis dibuja a 720 de alto fijos. La barra lateral vive entre la
           cabecera y el pie, asi que tiene 720 - 88 - 73 = 559. Con los
           margenes de Horizon, 40 arriba y 40 abajo, quedan 479 utiles.

           Siete pestanas de 70 son 490: ya se salian 11 antes de tocar nada.
           Sumando dos separadores de 28, son 546 y se salen 67, que es lo que
           empuja "Lenguaje" sobre el pie.

           Y no se arregla solo, porque Sidebar hereda de BoxLayout y no de
           ScrollView: lo que no cabe no se desplaza, se sale.

           Con estas medidas son 7*58 + 2*20 = 446 sobre 507 utiles. Entra, y
           sobra para una pestana mas: el static_assert de abajo lo comprueba al
           compilar, asi que si algun dia se anade otra y deja de caber, falla el
           build en vez de salirse en pantalla sin que nadie se entere. */
        constexpr unsigned SCREEN_HEIGHT = 720;
        constexpr unsigned HEADER_HEIGHT = 88;  // AppletFrame.headerHeightRegular
        constexpr unsigned FOOTER_HEIGHT = 73;  // AppletFrame.footerHeight

        constexpr unsigned SIDEBAR_ITEM_HEIGHT = 58;
        constexpr unsigned SIDEBAR_SEPARATOR_HEIGHT = 20;
        constexpr unsigned SIDEBAR_MARGIN = 26;

        constexpr unsigned MAX_TABS = 7;
        constexpr unsigned SEPARATORS = 2;

        constexpr unsigned SIDEBAR_AVAILABLE = SCREEN_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT - 2 * SIDEBAR_MARGIN;
        constexpr unsigned SIDEBAR_USED = MAX_TABS * SIDEBAR_ITEM_HEIGHT + SEPARATORS * SIDEBAR_SEPARATOR_HEIGHT;

        static_assert(SIDEBAR_USED <= SIDEBAR_AVAILABLE,
                      "La barra lateral no cabe en la pantalla: bajar la altura de las entradas, "
                      "la de los separadores o los margenes en oqb_theme.cpp");

        static_assert(SIDEBAR_USED + SIDEBAR_ITEM_HEIGHT <= SIDEBAR_AVAILABLE,
                      "No queda sitio para una pestana mas; ajustar las medidas antes de anadirla");

        class OqbStyle : public brls::HorizonStyle
        {
        public:
            OqbStyle()
            {
                this->Sidebar.Item.height = SIDEBAR_ITEM_HEIGHT;
                this->Sidebar.Separator.height = SIDEBAR_SEPARATOR_HEIGHT;
                this->Sidebar.marginTop = SIDEBAR_MARGIN;
                this->Sidebar.marginBottom = SIDEBAR_MARGIN;
            }
        };

    }  // namespace

    brls::Style* style()
    {
        // Lo libera borealis al cerrar.
        return new OqbStyle();
    }

    brls::LibraryViewsThemeVariantsWrapper* themeVariants()
    {
        // Lo libera borealis al cerrar: el wrapper borra las dos variantes.
        return new brls::LibraryViewsThemeVariantsWrapper(new OqbLightTheme(), new OqbDarkTheme());
    }

}  // namespace oqb
