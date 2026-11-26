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
        int seccion = 0;  // indice en Port::secciones (0 si el port no tiene secciones)
    };

    /* Una seccion de un port (4.8.0): el juego base, un DLC, una actualizacion,
       un pack de idioma o versiones que se excluyen (mismo grupo: Espanol /
       Ingles). Obligatoria si no es opcional ni de un grupo. */
    struct Seccion {
        std::string nombre;
        std::string grupo;     // de este grupo se elige una sola
        bool opcional = false;  // se marca o no
        bool marcada = true;    // si es opcional: como viene marcada
    };

    struct Port {
        std::string nombre;
        std::vector<Parte> partes;
        bool actualizacion = false;  // solo una actualizacion de otro port
        std::string nro;             // el .nro en la SD, si esta instalado (para el acceso directo)
        int imagen = -1;             // textura de la portada (-1 sin pedir, 0 sin icono)
        std::string fecha;           // la subida mas nueva de sus partes (ISO 8601), para «Recientes»

        long long total() const;
        // Del juego base (sin DLC ni actualizacion).
        int instaladas() const;
        // 0 sin instalar, 1 completo, 2 a medias (del juego base).
        int estado() const;
        std::vector<Seccion> secciones;  // vacio: un port de siempre (todas las partes juntas)
        std::vector<std::string> accesos;  // las apps con su propio acceso directo (4.8.1); vacio: una sola
        int partesDe(int seccion) const;
        int instaladasDe(int seccion) const;
        long long totalDe(int seccion) const;  // 0 si alguna parte no esta medida
        bool conSecciones() const { return !this->secciones.empty(); }
    };

    bool disponible(const nlohmann::ordered_json& nxlinks);

    // Abre la ficha del port; con A instala lo que falte (o todo de nuevo). Con
    // secciones, se marca en la ficha que instalar.
    void abrir(const nlohmann::ordered_json& nxlinks, const Port& port);

    // Instala esas partes del port, en orden.
    void instalar(const nlohmann::ordered_json& nxlinks, const Port& port, const std::vector<int>& aInstalar);

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
