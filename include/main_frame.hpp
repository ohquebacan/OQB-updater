#pragma once

#include <borealis.hpp>
#include <json.hpp>
#include <string>

#include "constants.hpp"
#include "iconos_oqb.hpp"

#include <utility>
#include <vector>

class MainFrame : public brls::TabFrame
{
private:
    // RefreshTask *refreshTask;

    // El último release publicado, consultado una vez al construir la ventana.
    // Vacío si no se pudo consultar.
    std::string latestTag;

    // El enlace de donacion: el del catalogo (enlaces.donar), o el de siempre.
    std::string donationUrl = DONATION_URL;

    // La pestana Apps, si hay versiones nuevas: se le dibuja encima el aviso.
    brls::View* appsItem = nullptr;

    // La pestana Nextendo mientras nunca se abrio: lleva un punto que invita.
    brls::View* nextendoItem = nullptr;
    // Nextendo funcionando (checks principales en verde): el punto queda encendido.
    bool nextendoFunciona = false;
    // La lista de packs (dentro de su pagina), para actualizar el pack desde Inicio.
    class AmsTab* tabPacks = nullptr;

    // Bacán Shop: el punto dorado (sin instalar y sin abrir, o version nueva).
    brls::View* bacanItem = nullptr;
    bool bacanNueva = false;
    bool bacanTrucos = false;  // base de trucos nueva

    // Para volver a calcular los avisos cuando termina una instalacion: el
    // catalogo con el que se armo la ventana y los items del menu (siempre,
    // aunque al abrir no tuvieran aviso).
    nlohmann::ordered_json catalogo;
    brls::View* appsSidebar = nullptr;
    brls::View* bacanSidebar = nullptr;
    int genInstalaciones = 0;
    void recalcularAvisos();

    // Rediseno: el icono de cada entrada del menu y las entradas a las que Inicio
    // lleva con A.
    std::vector<std::pair<brls::View*, iconos::Icono>> iconosMenu;
    brls::View* itemPacks = nullptr;
    brls::View* itemApps = nullptr;
    brls::View* itemTrucos = nullptr;
    brls::View* itemBacan = nullptr;
    brls::View* itemNextendo = nullptr;
    void irA(brls::View* item);

public:
    MainFrame();

    // Lo lento de la primera ventana (red y protecciones), para correrlo en un
    // hilo mientras se ve la pantalla de carga. Despues, MainFrame() lo usa sin
    // esperar.
    static void precargar();

    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
    void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override;

    const std::string& getLatestTag() const
    {
        return this->latestTag;
    }

    const std::string& getDonationUrl() const
    {
        return this->donationUrl;
    }
};
