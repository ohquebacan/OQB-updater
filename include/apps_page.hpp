#pragma once

#include <borealis.hpp>
#include <json.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

class ListDownloadTab;

/* Rediseno de Apps: buscador arriba, categorias como chips (L/R), la lista de
   apps con su icono y estado, y a la derecha la ficha de la elegida. Instalar o
   actualizar (A) pasa por la entrada de siempre de ListDownloadTab, que se arma
   oculta la primera vez que hace falta: la descarga, el acceso directo y todo
   lo demas siguen siendo los mismos. */
class AppsPage : public brls::View
{
public:
    explicit AppsPage(const nlohmann::ordered_json& nxlinks);
    ~AppsPage() override;

    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
    void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override {}
    brls::View* getDefaultFocus() override { return this; }
    void onFocusGained() override { this->focused = true; }
    void onFocusLost() override { this->focused = false; }

private:
    struct App {
        std::string nombre, url, categoria, desc, tengo, nueva;
        int estado = 0;  // 0 sin instalar, 1 instalada, 2 version nueva
        int imagen = -1;  // textura del icono (-1 sin pedir, 0 sin icono)
    };
    struct Chip {
        std::string texto;
        std::string clave;  // "" todas, "_nuevas" con actualizacion, o la seccion
    };
    nlohmann::ordered_json nxlinks;
    std::vector<App> apps;
    std::vector<Chip> chips;
    std::map<std::string, std::vector<std::string>> porSeccion;  // seccion -> nombres
    int chip = 0, elegida = 0, primera = 0;
    int genInstalaciones = 0;
    int fuente = -1;
    bool fuenteIntentada = false;
    ListDownloadTab* oculto = nullptr;  // las entradas de siempre, para instalar

    std::vector<int> visibles() const;
    void releer();
    void instalar();
    int icono(NVGcontext* vg, App& app);
};
