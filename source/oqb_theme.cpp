#include "oqb_theme.hpp"

#include <cstdio>

namespace oqb {

    namespace {

        struct Tema {
            const char* nombre;
            uint32_t fondo, panel, linea, claro, fuerte;
        };
        // Las mismas gamas que JKSV BACAN (source/theme.cpp), con Verde OQB primero.
        constexpr Tema TEMAS[] = {
            {"Verde OQB", 0x0B0F0A, 0x121711, 0x2A3327, 0x9DEA9A, 0x34D583},
            // Mas dorado que el de JKSV: el ambar de alla se confundia con el dorado de los avisos.
            {"Ámbar", 0x100E08, 0x16130B, 0x3D3522, 0xFFE27A, 0xF2C230},
            {"Bosque", 0x0C0F0D, 0x101411, 0x323B35, 0x8FC9A0, 0x6FB487},
            {"Grafito", 0x0E0F11, 0x131417, 0x373A41, 0xA9B8C8, 0x8AA0B6},
            {"Amatista", 0x0F0D12, 0x141118, 0x3A3243, 0xC0A8EE, 0xA68AE0},
            {"Rubí", 0x110C0D, 0x171012, 0x423034, 0xEE9A9A, 0xDE7C7C},
            // El verde lima del icono de la app (la Trifuerza de antes), mas neon.
            {"Neón", 0x0A0F08, 0x11170E, 0x2C3624, 0xCEF781, 0xB4EE86},
        };
        constexpr int N_TEMAS = sizeof(TEMAS) / sizeof(TEMAS[0]);
        constexpr const char TEMA_PATH[] = "/config/aio-switch-updater/tema.txt";

        int g_tema = 0;
        brls::Theme* g_oscuro = nullptr;
        brls::Theme* g_claro = nullptr;

        NVGcolor hex(uint32_t c) { return nvgRGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }
        NVGcolor mezcla(NVGcolor a, NVGcolor b, float p)
        {
            return nvgRGBf(a.r + (b.r - a.r) * p, a.g + (b.g - a.g) * p, a.b + (b.b - a.b) * p);
        }

        int leerTema()
        {
            int i = 0;
            if (FILE* f = std::fopen(TEMA_PATH, "r")) {
                if (std::fscanf(f, "%d", &i) != 1) i = 0;
                std::fclose(f);
            }
            return (i >= 0 && i < N_TEMAS) ? i : 0;
        }

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

        // Lo que depende del tema en los dos temas de borealis.
        void aplicarABorealis()
        {
            const Tema& t = TEMAS[g_tema];
            const NVGcolor claro = hex(t.claro), fuerte = hex(t.fuerte);
            if (g_oscuro && g_tema != 0) {
                const NVGcolor fondo = hex(t.fondo);
                g_oscuro->backgroundColor[0] = fondo.r;
                g_oscuro->backgroundColor[1] = fondo.g;
                g_oscuro->backgroundColor[2] = fondo.b;
                g_oscuro->backgroundColorRGB = fondo;
                g_oscuro->sidebarColor = hex(t.panel);
                g_oscuro->sidebarSeparatorColor = hex(t.linea);
                g_oscuro->listItemSeparatorColor = mezcla(fondo, hex(t.linea), 0.8f);
                g_oscuro->activeTabColor = claro;
                g_oscuro->highlightColor1 = claro;
                g_oscuro->highlightColor2 = mezcla(claro, nvgRGB(255, 255, 255), 0.35f);
                g_oscuro->highlightBackgroundColor = mezcla(fondo, claro, 0.07f);
                g_oscuro->listItemValueColor = claro;
                g_oscuro->headerRectangleColor = claro;
                g_oscuro->buttonPrimaryEnabledBackgroundColor = mezcla(claro, nvgRGB(255, 255, 255), 0.2f);
                g_oscuro->dialogButtonColor = claro;
            }
            if (g_claro && g_tema != 0) {
                const NVGcolor oscuroAcento = mezcla(fuerte, nvgRGB(0, 0, 0), 0.45f);
                g_claro->activeTabColor = oscuroAcento;
                g_claro->highlightColor1 = mezcla(fuerte, nvgRGB(0, 0, 0), 0.2f);
                g_claro->highlightColor2 = mezcla(claro, nvgRGB(0, 0, 0), 0.1f);
                g_claro->listItemValueColor = oscuroAcento;
                g_claro->headerRectangleColor = oscuroAcento;
                g_claro->buttonPrimaryEnabledBackgroundColor = oscuroAcento;
                g_claro->dialogButtonColor = oscuroAcento;
            }
        }
    }  // namespace

    int cantidadTemas() { return N_TEMAS; }
    const char* nombreTema(int i) { return (i >= 0 && i < N_TEMAS) ? TEMAS[i].nombre : ""; }
    int temaActual() { return g_tema; }

    NVGcolor acentoClaro()
    {
        const NVGcolor c = hex(TEMAS[g_tema].claro);
        return oscuro() ? c : mezcla(hex(TEMAS[g_tema].fuerte), nvgRGB(0, 0, 0), 0.4f);
    }

    NVGcolor acentoFuerte()
    {
        const NVGcolor c = hex(TEMAS[g_tema].fuerte);
        return oscuro() ? c : mezcla(c, nvgRGB(0, 0, 0), 0.35f);
    }

    NVGcolor acentoBase(bool fuerte)
    {
        return hex(fuerte ? TEMAS[g_tema].fuerte : TEMAS[g_tema].claro);
    }

    void elegirTema(int indice)
    {
        if (indice < 0 || indice >= N_TEMAS) return;
        /* Volver a Verde OQB con la app abierta: los temas de borealis ya se
           cambiaron, asi que se reponen sus valores de fabrica creando otros. */
        if (indice == 0 && g_tema != 0) {
            if (g_oscuro) *g_oscuro = OqbDarkTheme();
            if (g_claro) *g_claro = OqbLightTheme();
        }
        g_tema = indice;
        aplicarABorealis();
        if (FILE* f = std::fopen(TEMA_PATH, "w")) {
            std::fprintf(f, "%d\n", indice);
            std::fclose(f);
        }
    }

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
        constexpr unsigned HEADER_HEIGHT = 0;  // MainFrame va sin cabecera (rediseno)
        constexpr unsigned FOOTER_HEIGHT = 73;  // AppletFrame.footerHeight

        constexpr unsigned SIDEBAR_ITEM_HEIGHT = 62;
        constexpr unsigned SIDEBAR_SEPARATOR_HEIGHT = 20;
        constexpr unsigned SIDEBAR_MARGIN = 26;
        constexpr unsigned SIDEBAR_MARGIN_TOP = 124;  // el logo, en dos lineas, va arriba del panel

        // Rediseno: seis secciones sin separadores, siete si los trucos van aparte.
        constexpr unsigned MAX_TABS = 7;
        constexpr unsigned SEPARATORS = 0;

        constexpr unsigned SIDEBAR_AVAILABLE = SCREEN_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT - SIDEBAR_MARGIN_TOP - SIDEBAR_MARGIN;
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
                /* El marco de seleccion de borealis (menu lateral, listas, ventanas)
                   tan fino como el de las tarjetas del rediseno (3 px). */
                this->Highlight.strokeWidth = 3;
                /* Rediseno: el panel justo para icono y nombre. Borealis lo trae de
                   410 px con 88 de margen, y "Herramientas" es el nombre mas largo. */
                this->Sidebar.width = 262;
                this->Sidebar.marginLeft = 16;
                this->Sidebar.marginRight = 14;
                this->Sidebar.Separator.height = SIDEBAR_SEPARATOR_HEIGHT;
                this->Sidebar.marginTop = SIDEBAR_MARGIN_TOP;
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
        OqbLightTheme* claro = new OqbLightTheme();
        OqbDarkTheme* oscuroT = new OqbDarkTheme();
        g_claro = claro;
        g_oscuro = oscuroT;
        // El tema elegido en Herramientas, desde el arranque (la pantalla de carga ya sale con su color).
        g_tema = leerTema();
        aplicarABorealis();
        return new brls::LibraryViewsThemeVariantsWrapper(claro, oscuroT);
    }

}  // namespace oqb
