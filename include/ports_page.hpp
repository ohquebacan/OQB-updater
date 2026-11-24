#pragma once

#include <borealis.hpp>
#include <json.hpp>
#include <string>
#include <vector>

/* Pestana Ports (4.6.0): los ports del catalogo de alucardiio-yt como grilla
   de portadas, con chips (L/R) y buscador (X). Los que vienen en varias partes
   («X (PARTE 1)», «X (PARTE 2)») son un solo juego: el catalogo los agrupa en
   "ports" y aca se instalan todas sus partes juntas, una detras de otra
   (bajar, descomprimir y borrar el zip), siguiendo desde la que falte. */
namespace ports {

    struct Parte {
        std::string url;
        long long bytes = 0;  // 0 si el catalogo todavia no lo midio
        long long descomprimido = 0;  // lo que ocupa en la SD (0 si no se sabe)
        bool instalada = false;
    };

    struct Port {
        std::string nombre;
        std::vector<Parte> partes;
        bool actualizacion = false;  // solo una actualizacion de otro port
        std::string nro;             // el .nro en la SD, si esta instalado (para el acceso directo)
        int imagen = -1;             // textura de la portada (-1 sin pedir, 0 sin icono)
        std::string fecha;           // la subida mas nueva de sus partes (ISO 8601), para «Recientes»

        long long total() const;
        int instaladas() const;
        // 0 sin instalar, 1 completo, 2 a medias.
        int estado() const;
    };

    bool disponible(const nlohmann::ordered_json& nxlinks);

    // Abre la ficha del port; con A instala lo que falte (o todo de nuevo).
    void abrir(const nlohmann::ordered_json& nxlinks, const Port& port);

}  // namespace ports

class PortsPage : public brls::View
{
public:
    explicit PortsPage(const nlohmann::ordered_json& nxlinks);
    ~PortsPage() override;

    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
    void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override {}
    brls::View* getDefaultFocus() override { return this; }

private:
    nlohmann::ordered_json nxlinks;
    std::vector<ports::Port> todos;
    int chip = 0, elegido = 0, primeraFila = 0, columnas = 6;
    int orden = 0;  // 0 recientes, 1 mas pesados, 2 mas livianos, 3 A-Z (Y lo cambia)
    int genInstalaciones = -1;
    int fuente = -1;
    bool fuenteIntentada = false;
    std::string filtro;

    void releer();
    std::vector<int> visibles() const;
    int portada(NVGcontext* vg, ports::Port& p);
    void mover(int delta);
};
