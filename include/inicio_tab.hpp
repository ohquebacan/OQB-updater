#pragma once

#include <borealis.hpp>
#include <functional>
#include <string>
#include <vector>

// Inicio: lo que hay en la consola y lo que conviene hacer, de un vistazo.
// Reemplaza a Estado como primera pestana (Estado sigue a un boton de
// distancia). Todo lo que muestra ya lo calculo MainFrame al armar la ventana:
// aqui no se toca la red ni la SD.
class InicioTab : public brls::View
{
public:
    enum class Destino { Packs, App, Apps, Trucos, Partidas, Nextendo, Estado, Llaves, Warmboot };

    struct Datos {
        std::string ams, firmware, pack, packFecha, arranque, libre;
        bool packNuevo = false;          // hay una version nueva del pack que esta en la SD
        std::string packNuevoNombre, packNuevoFecha;
        std::string appVersion, appTag;  // la de esta app y la ultima publicada ("" sin conexion)
        bool appNueva = false;
        int appsNuevas = 0;
        std::string appsNombres;         // "sphaira, Akira y 1 mas"
        bool hayTrucos = false;          // la seccion existe (Bacán Shop o pestana de trucos)
        int trucosListos = -1;           // -1: todavia no se revisaron (sin cache)
        int trucosOtra = 0;
        int partidas = -1;               // -1: el catalogo no trae partidas
        bool hayNextendo = false, nextendoFunciona = false;
        int avisos = 0;                  // protecciones con algo para mirar
        bool proteccionFalla = false;    // alguna protección no esta activa
        // prod.keys: 0 al dia, 1 viejas, 2 no hay, 3 hay (sin saber de que firmware)
        int llavesEstado = 2;
        std::string llavesTexto;
        // Aviso de firmware que Atmosphère todavia no soporta ("" = sin aviso)
        std::string fwAviso;
        bool fwAvisoGrave = false;       // la consola ya esta en ese firmware
        bool fwAvisoWarmboot = false;    // Mariko y el catalogo trae el warmboot: la franja se elige y A lo instala
    };

    InicioTab(Datos datos, std::function<void(Destino)> ir);

    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
    void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override {}
    brls::View* getDefaultFocus() override { return this; }
    void onFocusGained() override;
    void onFocusLost() override;

private:
    struct Tarjeta {
        Destino destino;
        int fila, col;
    };
    Datos datos;
    std::function<void(Destino)> ir;
    std::vector<Tarjeta> tarjetas;
    int elegida = 0;
    int fuente = -1;
    bool fuenteIntentada = false;

    bool mover(int dFila, int dCol);
};
