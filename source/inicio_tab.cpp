#include "inicio_tab.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "iconos_oqb.hpp"
#include "oqb_theme.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {

    std::string cortar(NVGcontext* vg, const std::string& texto, float ancho)
    {
        if (nvgTextBounds(vg, 0, 0, texto.c_str(), nullptr, nullptr) <= ancho) return texto;
        std::string s = texto;
        while (!s.empty()) {
            do s.pop_back();
            while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80);
            if (!s.empty()) s.pop_back();
            const std::string prueba = s + "…";
            if (nvgTextBounds(vg, 0, 0, prueba.c_str(), nullptr, nullptr) <= ancho) return prueba;
        }
        return "…";
    }

    NVGcolor conAlfa(NVGcolor c, float a)
    {
        c.a *= a;
        return c;
    }

}  // namespace

InicioTab::InicioTab(Datos d, std::function<void(Destino)> irA) : datos(std::move(d)), ir(std::move(irA))
{
    // Arriba, dos tarjetas: el pack de la SD y esta app, cada una con su estado.
    // Fila 0: la ficha de prod.keys (la unica de los datos de arriba que se elige).
    // Fila 0: la franja del firmware nuevo, si se puede instalar el warmboot desde aqui.
    if (this->datos.fwAvisoWarmboot) this->tarjetas.push_back({Destino::Warmboot, 0, 0});
    this->tarjetas.push_back({Destino::Llaves, 1, 0});
    this->tarjetas.push_back({Destino::Packs, 2, 0});
    this->elegida = (int)this->tarjetas.size() - 1;  // se empieza en el pack, como antes
    this->tarjetas.push_back({Destino::App, 2, 1});
    int col = 0;
    this->tarjetas.push_back({Destino::Apps, 3, col++});
    if (this->datos.hayTrucos) this->tarjetas.push_back({Destino::Trucos, 3, col++});
    if (this->datos.partidas >= 0) this->tarjetas.push_back({Destino::Partidas, 3, col++});
    if (this->datos.hayNextendo) this->tarjetas.push_back({Destino::Nextendo, 3, col++});
    this->tarjetas.push_back({Destino::Estado, 4, 0});

    this->registerAction("", brls::Key::DUP, [this] { return this->mover(-1, 0); }, true);
    this->registerAction("", brls::Key::DDOWN, [this] { return this->mover(1, 0); }, true);
    // A la izquierda de la primera columna no se consume: borealis vuelve al menu.
    this->registerAction("", brls::Key::DLEFT, [this] { return this->mover(0, -1); }, true);
    this->registerAction("", brls::Key::DRIGHT, [this] { return this->mover(0, 1); }, true);
    this->registerAction("menus/inicio/abrir"_i18n, brls::Key::A, [this] {
        if (this->elegida >= 0 && this->elegida < (int)this->tarjetas.size()) this->ir(this->tarjetas[this->elegida].destino);
        return true;
    });
}

void InicioTab::onFocusGained()
{
    // Sin el recuadro de borealis alrededor de toda la pantalla: la tarjeta
    // elegida se marca sola.
    this->focused = true;
}

void InicioTab::onFocusLost()
{
    this->focused = false;
}

bool InicioTab::mover(int dFila, int dCol)
{
    if (this->tarjetas.empty()) return false;
    const Tarjeta actual = this->tarjetas[this->elegida];
    if (dCol) {
        for (int i = 0; i < (int)this->tarjetas.size(); ++i) {
            if (this->tarjetas[i].fila == actual.fila && this->tarjetas[i].col == actual.col + dCol) {
                this->elegida = i;
                return true;
            }
        }
        return false;  // borde: a la izquierda vuelve al menu
    }
    // Arriba/abajo: la fila siguiente que exista, en la columna mas cercana.
    for (int fila = actual.fila + dFila; fila >= 0 && fila <= 4; fila += dFila) {
        int mejor = -1, distancia = 1000;
        for (int i = 0; i < (int)this->tarjetas.size(); ++i) {
            if (this->tarjetas[i].fila != fila) continue;
            const int dd = std::abs(this->tarjetas[i].col - actual.col);
            if (dd < distancia) {
                distancia = dd;
                mejor = i;
            }
        }
        if (mejor >= 0) {
            this->elegida = mejor;
            return true;
        }
    }
    return true;  // arriba del todo o abajo del todo: se queda donde esta
}

void InicioTab::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    if (!this->fuenteIntentada) {
        this->fuenteIntentada = true;
        this->fuente = nvgFindFont(vg, "upheaval");
        if (this->fuente < 0) this->fuente = nvgCreateFont(vg, "upheaval", "romfs:/fonts/upheavtt.ttf");
    }
    const int regular = ctx->fontStash->regular;
    const int titulos = this->fuente >= 0 ? this->fuente : regular;
    const NVGcolor texto = this->a(ctx->theme->textColor);
    const NVGcolor tenue = this->a(ctx->theme->descriptionColor);
    const NVGcolor tarjeta = this->a(oqb::velo(oqb::oscuro() ? 12 : 10));
    const NVGcolor borde = this->a(ctx->theme->sidebarSeparatorColor);
    const NVGcolor foco = this->a(ctx->theme->highlightColor1);
    const bool conFoco = this->isFocused();

    const float px = x + 40.0f, ancho = width - 80.0f;
    float py = y + 30.0f;

    // Titulo.
    nvgFontFaceId(vg, titulos);
    nvgFontSize(vg, 40.0f);
    nvgTextLetterSpacing(vg, 1.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, texto);
    nvgBeginPath(vg);
    nvgText(vg, px, py, "menus/inicio/titulo"_i18n.c_str(), nullptr);
    nvgTextLetterSpacing(vg, 0.0f);
    nvgFontFaceId(vg, regular);
    nvgFontSize(vg, 17.0f);
    nvgFillColor(vg, tenue);
    nvgBeginPath(vg);
    nvgText(vg, px, py + 48.0f, "menus/inicio/subtitulo"_i18n.c_str(), nullptr);
    py += 84.0f;

    auto marcoFoco = [&](int indice, float bx, float by, float bw, float bh, float radio) {
        if (!conFoco || indice != this->elegida) return;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, bx - 3, by - 3, bw + 6, bh + 6, radio + 3);
        nvgStrokeColor(vg, foco);
        nvgStrokeWidth(vg, 3.0f);
        nvgStroke(vg);
    };

    /* Aviso de firmware nuevo que Atmosphère todavia no soporta (lo manda el
       catalogo). Franja discreta; roja si la consola ya esta en ese firmware. */
    if (!this->datos.fwAviso.empty()) {
        /* Mas notoria: algo mas gruesa, borde del color del aviso y un pulso
           suave (el fondo y el borde suben y bajan cada ~1,6 s). */
        const float t = std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const float pulso = 0.5f + 0.5f * std::sin(t * 3.9f);  // 0..1
        /* Rojizo (coral) para que no se confunda con el amarillo de "actualizacion
           pendiente"; rojo pleno si la consola ya esta en ese firmware. */
        const NVGcolor c = this->a(this->datos.fwAvisoGrave ? oqb::rojoTexto() : (oqb::oscuro() ? nvgRGB(255, 150, 120) : nvgRGB(170, 55, 30)));
        const NVGcolor base = this->datos.fwAvisoGrave ? nvgRGBA(226, 75, 74, 255) : nvgRGBA(235, 100, 70, 255);
        const float alfaFondo = (oqb::oscuro() ? 0.16f : 0.22f) + 0.14f * pulso;
        nvgFontSize(vg, 16.0f);
        nvgTextLineHeight(vg, 1.3f);
        float caja[4];
        nvgTextBoxBounds(vg, 0, 0, ancho - 64.0f, this->datos.fwAviso.c_str(), nullptr, caja);
        const float bh = std::max(52.0f, caja[3] - caja[1] + 32.0f);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, px, py - 8.0f, ancho, bh, 12.0f);
        nvgFillColor(vg, this->a(nvgRGBAf(base.r, base.g, base.b, alfaFondo)));
        nvgFill(vg);
        nvgStrokeColor(vg, this->a(nvgRGBAf(base.r, base.g, base.b, 0.35f + 0.5f * pulso)));
        nvgStrokeWidth(vg, 2.0f);
        nvgStroke(vg);
        iconos::dibujar(vg, iconos::Icono::Escudo, px + 24.0f, py - 8.0f + bh / 2, 20.0f, c);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgFillColor(vg, c);
        nvgBeginPath(vg);
        nvgTextBox(vg, px + 48.0f, py + 8.0f, ancho - 64.0f, this->datos.fwAviso.c_str(), nullptr);
        for (int i = 0; i < (int)this->tarjetas.size(); ++i)
            if (this->tarjetas[i].destino == Destino::Warmboot) marcoFoco(i, px, py - 8.0f, ancho, bh, 12.0f);
        py += bh + 10.0f;
    }

    // Fichas con los datos de la consola.
    {
        const std::pair<std::string, std::string> fichas[] = {
            {"menus/inicio/ams"_i18n, this->datos.ams},       {"menus/inicio/firmware"_i18n, this->datos.firmware},
            {"menus/inicio/pack"_i18n, this->datos.pack},     {"menus/inicio/arranque"_i18n, this->datos.arranque},
            {"menus/inicio/libre"_i18n, this->datos.libre}};
        float fx = px;
        nvgFontSize(vg, 17.0f);
        const float anchoLlaves = std::max(nvgTextBounds(vg, 0, 0, this->datos.llavesTexto.c_str(), nullptr, nullptr), 80.0f) + 42.0f;
        for (const auto& [etiqueta, valor] : fichas) {
            if (valor.empty()) continue;
            nvgFontSize(vg, 17.0f);
            const float w = std::max(nvgTextBounds(vg, 0, 0, valor.c_str(), nullptr, nullptr), 80.0f) + 32.0f;
            if (fx + w > px + ancho - anchoLlaves) break;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, fx, py, w, 60.0f, 12.0f);
            nvgFillColor(vg, tarjeta);
            nvgFill(vg);
            nvgFontSize(vg, 13.0f);
            nvgFillColor(vg, tenue);
            nvgBeginPath(vg);
            nvgText(vg, fx + 16.0f, py + 10.0f, etiqueta.c_str(), nullptr);
            nvgFontSize(vg, 17.0f);
            nvgFillColor(vg, texto);
            nvgBeginPath(vg);
            nvgText(vg, fx + 16.0f, py + 30.0f, valor.c_str(), nullptr);
            fx += w + 10.0f;
        }
        // prod.keys: la ficha que se elige (A explica y ofrece sacarlas).
        {
            const std::string valor = this->datos.llavesTexto;
            const NVGcolor c = this->a(this->datos.llavesEstado == 0 || this->datos.llavesEstado == 3 ? oqb::verdeTexto()
                                       : this->datos.llavesEstado == 1                                ? oqb::doradoTexto()
                                                                                                       : oqb::rojoTexto());
            nvgFontSize(vg, 17.0f);
            const float w = std::max(nvgTextBounds(vg, 0, 0, valor.c_str(), nullptr, nullptr), 80.0f) + 32.0f;
            const float lx = std::min(fx, px + ancho - w);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, lx, py, w, 60.0f, 12.0f);
            nvgFillColor(vg, tarjeta);
            nvgFill(vg);
            for (int i = 0; i < (int)this->tarjetas.size(); ++i)
                if (this->tarjetas[i].destino == Destino::Llaves) marcoFoco(i, lx, py, w, 60.0f, 12.0f);
            nvgFontSize(vg, 13.0f);
            nvgFillColor(vg, tenue);
            nvgBeginPath(vg);
            nvgText(vg, lx + 16.0f, py + 10.0f, "menus/inicio/llaves"_i18n.c_str(), nullptr);
            nvgFontSize(vg, 17.0f);
            nvgFillColor(vg, c);
            nvgBeginPath(vg);
            nvgText(vg, lx + 16.0f, py + 30.0f, valor.c_str(), nullptr);
        }
        py += 76.0f;
    }


    /* Arriba, dos tarjetas: el pack que esta en la SD y esta app. Dorado si hay
       version nueva (A la instala directo), verde si esta al dia. */
    {
        const float gap = 14.0f, bh = 92.0f, tw = (ancho - gap) / 2.0f;
        for (int i = 0; i < (int)this->tarjetas.size(); ++i) {
            const Tarjeta& t = this->tarjetas[i];
            if (t.fila != 2) continue;
            const float tx = px + t.col * (tw + gap);
            const bool esPack = t.destino == Destino::Packs;
            const bool nuevo = esPack ? this->datos.packNuevo : this->datos.appNueva;
            const NVGcolor c = this->a(nuevo ? oqb::doradoTexto() : oqb::verdeTexto());
            std::string titulo, sub, accion;
            if (esPack) {
                titulo = this->datos.pack.empty() ? "menus/inicio/pack_card"_i18n : this->datos.pack;
                sub = nuevo ? fmt::format("menus/inicio/pack_nueva"_i18n, this->datos.packNuevoFecha)
                            : (this->datos.pack.empty() ? "menus/inicio/pack_sin"_i18n : "menus/inicio/al_dia"_i18n);
                accion = nuevo ? "menus/inicio/boton_actualizar"_i18n : "menus/inicio/abrir"_i18n;
            }
            else {
                titulo = fmt::format("menus/inicio/app_card"_i18n, this->datos.appVersion);
                sub = this->datos.appTag.empty() ? "menus/inicio/app_sin_red"_i18n
                      : nuevo                    ? fmt::format("menus/inicio/app_nueva"_i18n, this->datos.appTag)
                                                 : "menus/inicio/al_dia"_i18n;
                accion = this->datos.appTag.empty() ? "" : (nuevo ? "menus/inicio/boton_actualizar"_i18n : "menus/inicio/reinstalar"_i18n);
            }
            nvgBeginPath(vg);
            nvgRoundedRect(vg, tx, py, tw, bh, 14.0f);
            nvgFillColor(vg, nuevo ? this->a(nvgRGBA(230, 185, 92, oqb::oscuro() ? 40 : 60)) : tarjeta);
            nvgFill(vg);
            marcoFoco(i, tx, py, tw, bh, 14.0f);
            iconos::dibujar(vg, esPack ? iconos::Icono::Packs : iconos::Icono::Descarga, tx + 34.0f, py + bh / 2, 26.0f, c);
            nvgFontSize(vg, 17.0f);
            const float anchoAccion = accion.empty() ? 0.0f : nvgTextBounds(vg, 0, 0, accion.c_str(), nullptr, nullptr) + 28.0f;
            const float textoAncho = tw - 62.0f - anchoAccion - 12.0f;
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgFillColor(vg, texto);
            nvgFontSize(vg, 19.0f);
            nvgBeginPath(vg);
            nvgText(vg, tx + 62.0f, py + 20.0f, cortar(vg, titulo, textoAncho).c_str(), nullptr);
            nvgFillColor(vg, c);
            nvgFontSize(vg, 15.0f);
            nvgBeginPath(vg);
            nvgText(vg, tx + 62.0f, py + 50.0f, cortar(vg, sub, textoAncho).c_str(), nullptr);
            if (!accion.empty()) {
                nvgFontSize(vg, 17.0f);
                nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                nvgFillColor(vg, c);
                nvgBeginPath(vg);
                nvgText(vg, tx + tw - 20.0f, py + bh / 2, accion.c_str(), nullptr);
            }
        }
        py += bh + 16.0f;
    }

    // Los accesos.
    {
        int columnas = 0;
        for (const auto& t : this->tarjetas)
            if (t.fila == 3) ++columnas;
        // Con el aviso de firmware arriba, los accesos un poco mas bajos para que entre todo.
        const float gap = 14.0f, th = this->datos.fwAviso.empty() ? 166.0f : 146.0f;
        const float tw = columnas ? (ancho - gap * (columnas - 1)) / columnas : ancho;
        for (int i = 0; i < (int)this->tarjetas.size(); ++i) {
            const Tarjeta& t = this->tarjetas[i];
            if (t.fila != 3) continue;
            const float tx = px + t.col * (tw + gap);
            std::string num, titulo, sub;
            iconos::Icono icono = iconos::Icono::Apps;
            NVGcolor color = oqb::verdeTexto();
            NVGcolor fondoIcono = nvgRGBA(52, 213, 131, 46);
            switch (t.destino) {
                case Destino::Apps:
                    num = std::to_string(this->datos.appsNuevas);
                    titulo = this->datos.appsNuevas ? "menus/inicio/apps"_i18n : "menus/inicio/apps_al_dia"_i18n;
                    sub = this->datos.appsNuevas ? this->datos.appsNombres : "menus/inicio/apps_al_dia_sub"_i18n;
                    break;
                case Destino::Trucos:
                    icono = iconos::Icono::Trucos;
                    color = oqb::doradoTexto();
                    fondoIcono = nvgRGBA(230, 185, 92, 46);
                    num = this->datos.trucosListos < 0 ? "—" : std::to_string(this->datos.trucosListos);
                    titulo = "menus/inicio/trucos"_i18n;
                    sub = this->datos.trucosListos < 0 ? "menus/inicio/trucos_revisar"_i18n
                                                       : (this->datos.trucosOtra ? fmt::format("menus/inicio/trucos_otra"_i18n, this->datos.trucosOtra)
                                                                                 : "menus/inicio/trucos_listos"_i18n);
                    break;
                case Destino::Partidas:
                    icono = iconos::Icono::Partidas;
                    num = std::to_string(this->datos.partidas);
                    titulo = "menus/inicio/partidas"_i18n;
                    sub = "menus/inicio/partidas_sub"_i18n;
                    break;
                case Destino::Nextendo:
                    icono = iconos::Icono::Nextendo;
                    color = oqb::oscuro() ? nvgRGB(187, 168, 255) : nvgRGB(75, 50, 168);
                    fondoIcono = nvgRGBA(150, 120, 255, 40);
                    num = this->datos.nextendoFunciona ? "ON" : "OFF";
                    titulo = "Nextendo";
                    sub = this->datos.nextendoFunciona ? "menus/inicio/nextendo_on"_i18n : "menus/inicio/nextendo_off"_i18n;
                    break;
                default: break;
            }
            nvgBeginPath(vg);
            nvgRoundedRect(vg, tx, py, tw, th, 16.0f);
            nvgFillColor(vg, tarjeta);
            nvgFill(vg);
            nvgStrokeColor(vg, borde);
            nvgStrokeWidth(vg, 1.0f);
            nvgStroke(vg);
            marcoFoco(i, tx, py, tw, th, 16.0f);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, tx + 18.0f, py + 18.0f, 40.0f, 40.0f, 11.0f);
            nvgFillColor(vg, this->a(fondoIcono));
            nvgFill(vg);
            iconos::dibujar(vg, icono, tx + 38.0f, py + 38.0f, 22.0f, this->a(color));
            const bool compacta = th < 160.0f;
            nvgFontFaceId(vg, titulos);
            nvgFontSize(vg, compacta ? 34.0f : 40.0f);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgFillColor(vg, texto);
            nvgBeginPath(vg);
            nvgText(vg, tx + 18.0f, py + (compacta ? 62.0f : 68.0f), num.c_str(), nullptr);
            nvgFontFaceId(vg, regular);
            nvgFontSize(vg, 16.0f);
            nvgBeginPath(vg);
            nvgText(vg, tx + 18.0f, py + th - (compacta ? 48.0f : 54.0f), cortar(vg, titulo, tw - 36.0f).c_str(), nullptr);
            nvgFontSize(vg, 14.0f);
            nvgFillColor(vg, tenue);
            nvgBeginPath(vg);
            nvgText(vg, tx + 18.0f, py + th - (compacta ? 26.0f : 30.0f), cortar(vg, sub, tw - 36.0f).c_str(), nullptr);
        }
        py += th + 16.0f;
    }

    // Estado y protección: una linea, a un boton de distancia.
    for (int i = 0; i < (int)this->tarjetas.size(); ++i) {
        if (this->tarjetas[i].destino != Destino::Estado) continue;
        const float bh = 54.0f;
        const NVGcolor violeta = oqb::oscuro() ? nvgRGB(187, 168, 255) : nvgRGB(75, 50, 168);   // el tono de Nextendo en Estado
        const NVGcolor c = this->a(this->datos.proteccionFalla ? oqb::rojoTexto()
                                   : this->datos.avisos ? oqb::doradoTexto()
                                   : this->datos.nextendoCambios ? violeta : oqb::verdeTexto());
        nvgBeginPath(vg);
        nvgRoundedRect(vg, px, py, ancho, bh, 14.0f);
        nvgFillColor(vg, tarjeta);
        nvgFill(vg);
        marcoFoco(i, px, py, ancho, bh, 14.0f);
        iconos::dibujar(vg, iconos::Icono::Escudo, px + 30.0f, py + bh / 2, 22.0f, c);
        const std::string estaNand = this->datos.nextendoOtraNand == "emuNAND" ? "SysNAND" : "EmuNAND";
        const std::string linea = this->datos.proteccionFalla ? "menus/inicio/proteccion_falla"_i18n
                                  : this->datos.avisos         ? fmt::format("menus/inicio/proteccion_avisos"_i18n, this->datos.avisos)
                                  : this->datos.nextendoCambios ? fmt::format("menus/inicio/proteccion_nextendo"_i18n, this->datos.nextendoCambios)
                                  : !this->datos.nextendoOtraNand.empty() ? fmt::format("menus/inicio/proteccion_otra_nand"_i18n, estaNand, this->datos.nextendoOtraNand)
                                                               : "menus/inicio/proteccion_ok"_i18n;
        nvgFontSize(vg, 17.0f);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, c);
        nvgBeginPath(vg);
        nvgText(vg, px + 54.0f, py + bh / 2, linea.c_str(), nullptr);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, tenue);
        nvgBeginPath(vg);
        nvgText(vg, px + ancho - 20.0f, py + bh / 2, "menus/inicio/ver_estado"_i18n.c_str(), nullptr);
    }
    (void)conAlfa;
}
