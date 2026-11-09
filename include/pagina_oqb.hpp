#pragma once

#include <borealis.hpp>
#include <functional>
#include <string>
#include <vector>

// Una cabecera de seccion que se puede leer despues (la de borealis guarda el
// texto en privado). Las listas que se convierten a PaginaOqb usan esta.
class HeaderOqb : public brls::Header
{
public:
    explicit HeaderOqb(const std::string& texto) : brls::Header(texto), texto(texto) {}
    std::string texto;
};

// Una vista propia que deja lugar a su derecha para un boton: PaginaOqb dibuja
// ahi, dentro de la misma ficha, la entrada que la sigue (Bacán Shop: instalar).
struct ConBoton {
    float reservaDerecha = 0.0f;
};

// Una entrada con icono (las apps sugeridas de Bacán Shop).
struct ConIcono {
    std::string iconoRuta;               // archivo (cache del catalogo o generico)
    std::vector<uint8_t> iconoBytes;     // o el JPEG del .nro instalado
    int iconoImagen = -1;                // textura, la crea PaginaOqb
};

/* Rediseno: una pagina con titulo, armada a partir de una brls::List que ya
   existe (Packs, Herramientas): cada cabecera es una seccion, cada etiqueta su
   introduccion y cada ListItem una entrada con su titulo, subtitulo,
   descripcion y valor. A hace lo mismo que el click de la entrada original y X
   lo que tuviera registrado en X, asi que la logica sigue donde estaba. La
   lista original queda viva (no se ve) y se libera con la pagina.

   Dos formas:
   - Tarjetas: cada entrada es una tarjeta, en `columnas` por fila; con
     `botonA`, la tarjeta lleva abajo sus botones (A y, si la entrada tiene X, X).
   - Grupos: cada seccion es una caja (dos por fila) con sus entradas como filas. */
class PaginaOqb : public brls::View
{
public:
    // Secciones: cada seccion con su titulo, sus textos y sus vistas propias a lo
    // ancho (la ficha de Bacán Shop, el QR) y sus entradas como tarjetas.
    enum class Forma { Tarjetas, Grupos, Secciones };
    struct Opciones {
        Forma forma = Forma::Tarjetas;
        int columnas = 2;
        std::string botonA;       // texto del boton A en la tarjeta ("Instalar"); vacio = sin botones
        std::string botonAviso;   // el del boton A si la entrada tiene un aviso ("Actualizar")
        std::string hintX;        // texto de X en el pie (y en la tarjeta); vacio = sin X
    };

    PaginaOqb(const std::string& titulo, const std::string& subtitulo, brls::List* origen, Opciones opciones);
    ~PaginaOqb() override;

    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
    void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override {}
    brls::View* getDefaultFocus() override { return this->entradas.empty() ? nullptr : this; }
    void onFocusGained() override { this->focused = true; }
    void onFocusLost() override { this->focused = false; }
    // La lista original se entera de que se muestra (Bacán Shop y Nextendo
    // marcan ahi que ya se abrieron).
    void willAppear(bool resetState) override
    {
        if (this->origen) this->origen->willAppear(resetState);
        brls::View::willAppear(resetState);
    }

private:
    struct Entrada {
        brls::ListItem* item;
        std::string titulo, desc;
        int seccion;
        // Donde quedo dibujada (coordenadas de la pagina, sin desplazar), para
        // moverse por cercania y para que el desplazamiento la deje a la vista.
        float x = 0, y = 0, w = 0, h = 0;
    };
    struct Elemento {
        int entrada = -1;           // una entrada (tarjeta)
        brls::View* bloque = nullptr;  // una vista propia, se dibuja tal cual
        std::string texto;          // un parrafo
    };
    struct Seccion {
        std::string titulo, intro;
        std::vector<int> entradas;
        std::vector<Elemento> orden;  // todo, en el orden de la lista original
    };
    std::string titulo, subtitulo;
    brls::List* origen;
    Opciones op;
    std::vector<Seccion> secciones;
    std::vector<Entrada> entradas;
    /* Lo que se elige con la cruceta: una entrada entera, o en las tarjetas con
       botones, cada boton (0: el de A, 1: el de X). */
    struct Objetivo {
        int entrada, boton;
        float x = 0, y = 0, w = 0, h = 0;
    };
    std::vector<Objetivo> objetivos;
    int elegida = 0;
    float scroll = 0.0f, scrollObjetivo = 0.0f;
    int fuente = -1;
    int fuenteFuerte = -1;  // Bebas Neue: titulos de seccion y la etiqueta INSTALADA
    bool fuenteIntentada = false;

    void leerOrigen();
    bool mover(int dx, int dy);
    bool tieneX(brls::ListItem* item) const;
};
