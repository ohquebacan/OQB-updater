#include "pagina_oqb.hpp"

#include <algorithm>
#include <cmath>

#include "oqb_theme.hpp"
#include "pack_list_item.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {

    std::string cortar(NVGcontext* vg, const std::string& texto, float ancho)
    {
        if (ancho <= 0) return "";
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

    // Cuantas lineas ocupa un texto (como mucho `maximo`).
    int lineas(NVGcontext* vg, const std::string& t, float ancho, int maximo)
    {
        if (t.empty()) return 0;
        NVGtextRow fila[1];
        const char* p = t.c_str();
        const char* fin = p + t.size();
        int n = 0;
        while (n < maximo && p && p < fin && nvgTextBreakLines(vg, p, fin, ancho, fila, 1) > 0) {
            ++n;
            p = fila[0].next;
        }
        return n;
    }

    // Hasta `maximo` lineas; la ultima con "…" si sobra texto.
    void parrafo(NVGcontext* vg, float x, float y, const std::string& t, float ancho, float alto, int maximo)
    {
        NVGtextRow fila[1];
        const char* p = t.c_str();
        const char* fin = p + t.size();
        for (int i = 0; i < maximo && p && p < fin; ++i) {
            if (nvgTextBreakLines(vg, p, fin, ancho, fila, 1) <= 0) break;
            std::string linea(fila[0].start, fila[0].end);
            if (i == maximo - 1 && fila[0].next && fila[0].next < fin) linea = cortar(vg, linea + " " + std::string(fila[0].next, fin), ancho);
            nvgBeginPath(vg);
            nvgText(vg, x, y + i * alto, linea.c_str(), nullptr);
            p = fila[0].next;
        }
    }

    // Colores por grupo en la forma Grupos: verde, dorado, azul, violeta.
    NVGcolor colorGrupo(int i)
    {
        switch (i % 4) {
            case 0: return oqb::verdeTexto();
            case 1: return oqb::doradoTexto();
            case 2: return oqb::oscuro() ? nvgRGB(142, 180, 230) : nvgRGB(31, 78, 133);
            default: return oqb::oscuro() ? nvgRGB(187, 168, 255) : nvgRGB(75, 50, 168);
        }
    }

}  // namespace

PaginaOqb::PaginaOqb(const std::string& titulo, const std::string& subtitulo, brls::List* origen, Opciones opciones)
    : titulo(titulo), subtitulo(subtitulo), origen(origen), op(std::move(opciones))
{
    this->op.columnas = std::max(1, this->op.columnas);
    this->leerOrigen();

    this->registerAction("", brls::Key::DUP, [this] { return this->mover(0, -1); }, true);
    this->registerAction("", brls::Key::DDOWN, [this] { return this->mover(0, 1); }, true);
    this->registerAction("", brls::Key::DLEFT, [this] { return this->mover(-1, 0); }, true);
    this->registerAction("", brls::Key::DRIGHT, [this] { return this->mover(1, 0); }, true);
    this->registerAction("menus/inicio/abrir"_i18n, brls::Key::A, [this] {
        if (this->elegida < 0 || this->elegida >= (int)this->objetivos.size()) return true;
        const Objetivo& o = this->objetivos[this->elegida];
        brls::ListItem* item = this->entradas[o.entrada].item;
        if (o.boton == 1) {
            // El segundo boton hace lo que la entrada tenia en X (instalacion limpia).
            for (const auto& accion : item->getActions())
                if (accion.key == brls::Key::X && accion.available) return accion.actionListener();
            return true;
        }
        item->onClick();
        return true;
    });
}

PaginaOqb::~PaginaOqb()
{
    NVGcontext* vg = brls::Application::getNVGContext();
    for (const auto& e : this->entradas)
        if (auto* ci = dynamic_cast<ConIcono*>(e.item))
            if (ci->iconoImagen > 0) nvgDeleteImage(vg, ci->iconoImagen);
    delete this->origen;  // y con ella sus entradas
}

void PaginaOqb::leerOrigen()
{
    this->secciones.assign(1, {});
    this->entradas.clear();
    this->objetivos.clear();
    if (!this->origen) return;
    for (size_t i = 0; i < this->origen->getViewsCount(); ++i) {
        brls::View* v = this->origen->getChild(i);
        if (auto* h = dynamic_cast<HeaderOqb*>(v)) {
            Seccion& ultima = this->secciones.back();
            if (!ultima.entradas.empty() || !ultima.intro.empty() || !ultima.titulo.empty()) this->secciones.push_back({});
            this->secciones.back().titulo = h->texto;
            this->secciones.back().columnas = h->columnas;
        }
        else if (auto* l = dynamic_cast<brls::Label*>(v)) {
            auto& intro = this->secciones.back().intro;
            intro += (intro.empty() ? "" : "\n") + l->getText();
            this->secciones.back().orden.push_back({-1, nullptr, l->getText()});
        }
        else if (auto* item = dynamic_cast<brls::ListItem*>(v)) {
            this->secciones.back().orden.push_back({(int)this->entradas.size(), nullptr, ""});
            this->secciones.back().entradas.push_back(this->entradas.size());
            this->entradas.push_back({item, item->getLabel(), item->getDescription(), (int)this->secciones.size() - 1});
        }
        else if (v && !dynamic_cast<brls::Header*>(v)) {
            this->secciones.back().orden.push_back({-1, v, ""});
        }
    }
    const bool botones = this->op.forma == Forma::Tarjetas && !this->op.botonA.empty();
    for (int i = 0; i < (int)this->entradas.size(); ++i) {
        if (!botones) {
            this->objetivos.push_back({i, -1});
            continue;
        }
        this->objetivos.push_back({i, 0});
        if (!this->op.hintX.empty() && this->tieneX(this->entradas[i].item)) this->objetivos.push_back({i, 1});
    }
}

void PaginaOqb::refrescarOrigen()
{
    auto* r = dynamic_cast<Recargable*>(this->origen);
    if (!r || !r->recargar()) return;
    // La lista se armo de nuevo: se vuelven a leer sus entradas, dejando el foco donde estaba.
    this->leerOrigen();
    if (this->elegida >= (int)this->objetivos.size()) this->elegida = std::max(0, (int)this->objetivos.size() - 1);
}

bool PaginaOqb::tieneX(brls::ListItem* item) const
{
    for (const auto& accion : item->getActions())
        if (accion.key == brls::Key::X && accion.available) return true;
    return false;
}

bool PaginaOqb::mover(int dx, int dy)
{
    if (this->objetivos.empty()) return false;
    const Objetivo& a = this->objetivos[this->elegida];
    const float ax = a.x + a.w / 2, ay = a.y + a.h / 2;
    int mejor = -1;
    float mejorD = 1e9f;
    for (int i = 0; i < (int)this->objetivos.size(); ++i) {
        if (i == this->elegida) continue;
        const Objetivo& b = this->objetivos[i];
        const float bx = b.x + b.w / 2, by = b.y + b.h / 2;
        // Solo lo que esta de verdad en esa direccion; se prefiere lo alineado.
        float principal, lateral;
        if (dx) {
            principal = (bx - ax) * dx;
            lateral = std::fabs(by - ay);
            if (principal <= 1.0f || lateral > std::max(a.h, b.h)) continue;
        }
        else {
            principal = (by - ay) * dy;
            /* Lo que se aleja en horizontal, no la distancia entre centros: una
               tarjeta a todo lo ancho tiene el centro lejos de una de media fila y
               perdia contra una mas abajo pero alineada, asi que nunca se elegia. */
            lateral = std::max(0.0f, std::max(a.x, b.x) - std::min(a.x + a.w, b.x + b.w));
            if (principal <= 1.0f) continue;
        }
        const float d = principal + lateral * 2.0f;
        if (d < mejorD) {
            mejorD = d;
            mejor = i;
        }
    }
    if (mejor >= 0) {
        this->elegida = mejor;
        return true;
    }
    if (dy < 0) this->scrollObjetivo = 0.0f;  // arriba del todo: que se vea el titulo
    return dx >= 0;  // a la izquierda del todo vuelve al menu
}

void PaginaOqb::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    if (!this->fuenteIntentada) {
        this->fuenteIntentada = true;
        this->fuente = nvgFindFont(vg, "upheaval");
        if (this->fuente < 0) this->fuente = nvgCreateFont(vg, "upheaval", "romfs:/fonts/upheavtt.ttf");
        // Upheaval no trae tildes (la Á de BACÁN): esas salen de la fuente normal.
        if (this->fuente >= 0) nvgAddFallbackFontId(vg, this->fuente, ctx->fontStash->regular);
        this->fuenteFuerte = nvgFindFont(vg, "bebas");
        if (this->fuenteFuerte < 0) this->fuenteFuerte = nvgCreateFont(vg, "bebas", "romfs:/fonts/BebasNeue-Regular.ttf");
        if (this->fuenteFuerte >= 0) nvgAddFallbackFontId(vg, this->fuenteFuerte, ctx->fontStash->regular);
    }
    const int regular = ctx->fontStash->regular;
    const NVGcolor texto = this->a(ctx->theme->textColor);
    const NVGcolor tenue = this->a(ctx->theme->descriptionColor);
    const NVGcolor fondo = this->a(oqb::velo(oqb::oscuro() ? 12 : 10));
    const NVGcolor fondoFila = this->a(oqb::velo(oqb::oscuro() ? 22 : 18));
    const NVGcolor borde = this->a(ctx->theme->sidebarSeparatorColor);
    const NVGcolor foco = this->a(ctx->theme->highlightColor1);
    const bool conFoco = this->isFocused();

    this->scroll += (this->scrollObjetivo - this->scroll) * 0.3f;
    if (std::fabs(this->scrollObjetivo - this->scroll) < 0.5f) this->scroll = this->scrollObjetivo;

    nvgSave(vg);
    nvgScissor(vg, x, y, width, height);
    const float ox = x, oy = y - this->scroll;  // origen de la pagina en pantalla
    const float px = 40.0f, ancho = width - 80.0f;
    float py = 30.0f;

    // Titulo.
    nvgFontFaceId(vg, this->fuente >= 0 ? this->fuente : regular);
    nvgFontSize(vg, 40.0f);
    nvgTextLetterSpacing(vg, 1.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, texto);
    nvgBeginPath(vg);
    nvgText(vg, ox + px, oy + py, this->titulo.c_str(), nullptr);
    nvgTextLetterSpacing(vg, 0.0f);
    nvgFontFaceId(vg, regular);
    if (!this->subtitulo.empty()) {
        nvgFontSize(vg, 17.0f);
        nvgFillColor(vg, tenue);
        nvgBeginPath(vg);
        nvgText(vg, ox + px, oy + py + 48.0f, this->subtitulo.c_str(), nullptr);
    }
    py += 86.0f;

    // Las introducciones de todas las secciones, juntas debajo del titulo.
    for (const Seccion& sec : this->op.forma == Forma::Secciones ? std::vector<Seccion>{} : this->secciones) {
        if (sec.intro.empty()) continue;
        nvgFontSize(vg, 16.0f);
        nvgTextLineHeight(vg, 1.3f);
        nvgFillColor(vg, tenue);
        const int n = lineas(vg, sec.intro, ancho, 3);
        parrafo(vg, ox + px, oy + py, sec.intro, ancho, 21.0f, 3);
        py += n * 21.0f + 12.0f;
    }

    auto pildora = [&](float vx, float cy, const std::string& t, int tono, float tam) {
        nvgFontSize(vg, tam);
        const float w = nvgTextBounds(vg, 0, 0, t.c_str(), nullptr, nullptr) + 22.0f, h = tam + 12.0f;
        if (tono) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, vx, cy - h / 2, w, h, h / 2);
            nvgFillColor(vg, this->a(tono == 1 ? nvgRGBA(52, 213, 131, 46) : nvgRGBA(230, 185, 92, oqb::oscuro() ? 46 : 70)));
            nvgFill(vg);
        }
        nvgFillColor(vg, this->a(tono == 1 ? oqb::verdeTexto() : (tono == 2 ? oqb::doradoTexto() : ctx->theme->descriptionColor)));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgBeginPath(vg);
        nvgText(vg, vx + w / 2, cy + 1, t.c_str(), nullptr);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        return w;
    };
    auto tonoDe = [](brls::ListItem* item) {
        if (auto* p = dynamic_cast<PackListItem*>(item)) return p->esPositivo() ? 1 : (p->esAviso() ? 2 : 0);
        return 0;
    };
    // Donde quedo cada objetivo (coordenadas de la pagina) y si es el elegido.
    auto ubicar = [&](int entrada, int boton, float ex, float ey, float ew, float eh) {
        for (int i = 0; i < (int)this->objetivos.size(); ++i) {
            Objetivo& o = this->objetivos[i];
            if (o.entrada == entrada && o.boton == boton) {
                o.x = ex;
                o.y = ey;
                o.w = ew;
                o.h = eh;
                return i == this->elegida;
            }
        }
        return false;
    };
    auto marco = [&](float ex, float ey, float ew, float eh, float radio) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, ox + ex - 3, oy + ey - 3, ew + 6, eh + 6, radio + 3);
        nvgStrokeColor(vg, foco);
        nvgStrokeWidth(vg, 3.0f);
        nvgStroke(vg);
    };

    if (this->op.forma == Forma::Secciones) {
        const float gap = 16.0f;
        int cols = this->op.columnas;
        float tw = (ancho - gap * (cols - 1)) / cols;
        auto altoTarjeta = [&](const Entrada& e) {
            nvgFontSize(vg, 19.0f);
            float h = 20.0f + lineas(vg, e.titulo, tw - 44.0f, 2) * 25.0f;
            const bool ambos = !e.item->getSubLabel().empty() && !e.item->getValue().empty();
            if (!e.item->getSubLabel().empty() || !e.item->getValue().empty()) h += 32.0f;
            nvgFontSize(vg, 15.0f);
            if (ambos) h += lineas(vg, e.item->getSubLabel(), tw - 44.0f, 2) * 20.0f + 2.0f;
            // Sin valor, el subtitulo puede ocupar dos lineas (antes se cortaba con «…»).
            else if (!e.item->getSubLabel().empty()) h += (lineas(vg, e.item->getSubLabel(), tw - 44.0f, 2) - 1) * 20.0f;
            if (!e.desc.empty()) h += lineas(vg, e.desc, tw - 44.0f, 3) * 20.0f + 6.0f;
            return std::max(h + 16.0f, dynamic_cast<ConIcono*>(e.item) ? 84.0f : 70.0f);
        };
        for (const Seccion& sec : this->secciones) {
            // Cada seccion puede pedir sus propias columnas (Nexus: tres tarjetas chicas).
            cols = sec.columnas > 0 ? sec.columnas : this->op.columnas;
            tw = (ancho - gap * (cols - 1)) / cols;
            if (!sec.titulo.empty()) {
                nvgFontSize(vg, 16.0f);
                nvgFillColor(vg, this->a(oqb::verdeTexto()));
                nvgBeginPath(vg);
                nvgText(vg, ox + px + 4, oy + py, sec.titulo.c_str(), nullptr);
                py += 30.0f;
            }
            for (size_t i = 0; i < sec.orden.size();) {
                const Elemento& el = sec.orden[i];
                if (!el.texto.empty()) {
                    nvgFontSize(vg, 16.0f);
                    nvgTextLineHeight(vg, 1.3f);
                    nvgFillColor(vg, tenue);
                    const int n = lineas(vg, el.texto, ancho, 8);
                    parrafo(vg, ox + px + 4, oy + py, el.texto, ancho - 8, 21.0f, 8);
                    py += n * 21.0f + 12.0f;
                    ++i;
                    continue;
                }
                if (el.bloque) {
                    // Una vista propia (la ficha, el QR, la tarjeta del USB): se dibuja tal cual, a lo ancho.
                    auto* conBoton = dynamic_cast<ConBoton*>(el.bloque);
                    // Las entradas que la siguen, como botones dentro de la vista, donde ella diga.
                    size_t nb = 0;
                    if (conBoton)
                        while ((int)nb < conBoton->botones() && i + 1 + nb < sec.orden.size() && sec.orden[i + 1 + nb].entrada >= 0) ++nb;
                    if (conBoton) conBoton->reservaDerecha = nb ? 230.0f : 0.0f;
                    const float h = std::max(el.bloque->getHeight(), 40u);
                    el.bloque->setBoundaries(ox + px, oy + py, ancho, h);
                    el.bloque->invalidate(true);
                    el.bloque->frame(ctx);
                    for (size_t k = 0; k < nb; ++k) {
                        const int indice = sec.orden[i + 1 + k].entrada;
                        Entrada& e = this->entradas[indice];
                        float bx, by, bw, bh;
                        conBoton->lugarBoton((int)k, ancho, h, bx, by, bw, bh);
                        e.x = px + bx;
                        e.y = py + by;
                        e.w = bw;
                        e.h = bh;
                        const bool relleno = tonoDe(e.item) == 2 || !e.item->getValue().empty();
                        nvgBeginPath(vg);
                        nvgRoundedRect(vg, ox + e.x, oy + e.y, bw, bh, 12.0f);
                        nvgFillColor(vg, relleno ? this->a(oqb::esmeralda()) : fondoFila);
                        nvgFill(vg);
                        nvgStrokeColor(vg, borde);
                        nvgStrokeWidth(vg, 1.0f);
                        nvgStroke(vg);
                        nvgFontSize(vg, 16.0f);
                        nvgFillColor(vg, relleno ? this->a(oqb::oscuro() ? nvgRGB(6, 20, 12) : nvgRGB(255, 255, 255)) : texto);
                        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                        nvgBeginPath(vg);
                        nvgText(vg, ox + e.x + bw / 2, oy + e.y + bh / 2 + 1, cortar(vg, e.titulo, bw - 16).c_str(), nullptr);
                        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                        if (ubicar(indice, -1, e.x, e.y, bw, bh) && conFoco) marco(e.x, e.y, bw, bh, 12.0f);
                    }
                    i += 1 + nb;
                    py += h + 12.0f;
                    continue;
                }
                // Entradas seguidas: en filas de `cols` tarjetas.
                size_t j = i;
                while (j < sec.orden.size() && sec.orden[j].entrada >= 0 && j - i < (size_t)cols) ++j;
                // Una tarjeta sola en su fila ocupa todo el ancho.
                const float twFila = j - i == 1 ? ancho : tw;
                float alto = 0;
                for (size_t k = i; k < j; ++k) alto = std::max(alto, altoTarjeta(this->entradas[sec.orden[k].entrada]));
                for (size_t k = i; k < j; ++k) {
                    const int indice = sec.orden[k].entrada;
                    Entrada& e = this->entradas[indice];
                    e.x = px + (k - i) * (twFila + gap);
                    e.y = py;
                    e.w = twFila;
                    e.h = alto;
                    const float cx = ox + e.x, cy = oy + e.y;
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, cx, cy, twFila, alto, 14.0f);
                    nvgFillColor(vg, fondo);
                    nvgFill(vg);
                    nvgStrokeColor(vg, borde);
                    nvgStrokeWidth(vg, 1.0f);
                    nvgStroke(vg);
                    if (ubicar(indice, -1, e.x, e.y, twFila, alto) && conFoco) marco(e.x, e.y, twFila, alto, 14.0f);
                    // Con icono (apps sugeridas), el texto empieza despues del icono.
                    float tx0 = cx + 20, twTexto = twFila - 40.0f;
                    if (auto* ci = dynamic_cast<ConIcono*>(e.item)) {
                        if (ci->iconoImagen < 0) {
                            ci->iconoImagen = 0;
                            if (!ci->iconoBytes.empty()) ci->iconoImagen = nvgCreateImageMem(vg, 0, ci->iconoBytes.data(), ci->iconoBytes.size());
                            else if (!ci->iconoRuta.empty()) ci->iconoImagen = nvgCreateImage(vg, ci->iconoRuta.c_str(), 0);
                            if (ci->iconoImagen < 0) ci->iconoImagen = 0;
                        }
                        const float lado = 52.0f;
                        if (ci->iconoImagen > 0) {
                            nvgBeginPath(vg);
                            nvgRoundedRect(vg, cx + 16, cy + 16, lado, lado, 12.0f);
                            nvgFillPaint(vg, nvgImagePattern(vg, cx + 16, cy + 16, lado, lado, 0, ci->iconoImagen, this->alpha));
                            nvgFill(vg);
                        }
                        tx0 = cx + 16 + lado + 14;
                        twTexto = twFila - (tx0 - cx) - 20;
                    }
                    float ty = cy + 18.0f;
                    nvgFontSize(vg, 19.0f);
                    const auto* pli = dynamic_cast<PackListItem*>(e.item);
                    nvgFillColor(vg, pli && pli->esPeligro() ? this->a(oqb::rojoTexto()) : texto);
                    parrafo(vg, tx0, ty, e.titulo, twTexto, 25.0f, 2);
                    ty += lineas(vg, e.titulo, twTexto, 2) * 25.0f + 2.0f;
                    const std::string sub = e.item->getSubLabel(), valor = e.item->getValue();
                    if (!sub.empty() && !valor.empty()) {
                        /* El estado (Instalada, JKSV BACAN...) en su propia linea y la
                           descripcion debajo: juntos en una se leian como una sola frase. */
                        pildora(tx0, ty + 13.0f, cortar(vg, valor, twTexto), tonoDe(e.item), 14.0f);
                        ty += 32.0f;
                        nvgFontSize(vg, 15.0f);
                        nvgTextLineHeight(vg, 1.3f);
                        nvgFillColor(vg, tenue);
                        parrafo(vg, tx0, ty, sub, twTexto, 20.0f, 2);
                        ty += lineas(vg, sub, twTexto, 2) * 20.0f + 2.0f;
                    }
                    else if (!sub.empty() && valor.empty()) {
                        // Solo subtitulo: hasta dos lineas.
                        nvgFontSize(vg, 15.0f);
                        nvgTextLineHeight(vg, 1.3f);
                        nvgFillColor(vg, tenue);
                        parrafo(vg, tx0, ty + 4.0f, sub, twTexto, 20.0f, 2);
                        ty += std::max(32.0f, lineas(vg, sub, twTexto, 2) * 20.0f + 12.0f);
                    }
                    else if (!sub.empty() || !valor.empty()) {
                        float sx = tx0;
                        if (!valor.empty()) sx += pildora(sx, ty + 13.0f, cortar(vg, valor, twTexto * 0.55f), tonoDe(e.item), 14.0f) + 10.0f;
                        if (!sub.empty()) {
                            nvgFontSize(vg, 15.0f);
                            nvgFillColor(vg, tenue);
                            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                            nvgBeginPath(vg);
                            nvgText(vg, sx, ty + 13.0f, cortar(vg, sub, cx + twFila - 20 - sx).c_str(), nullptr);
                            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                        }
                        ty += 32.0f;
                    }
                    if (!e.desc.empty()) {
                        nvgFontSize(vg, 15.0f);
                        nvgTextLineHeight(vg, 1.3f);
                        nvgFillColor(vg, tenue);
                        parrafo(vg, tx0, ty + 2.0f, e.desc, twTexto, 20.0f, 3);
                    }
                }
                py += alto + gap;
                i = j;
            }
            py += 10.0f;
        }
    }
    else if (this->op.forma == Forma::Tarjetas) {
        /* Todas las tarjetas en una grilla, una al lado de la otra; el titulo de
           su seccion va encima de la primera tarjeta de cada seccion. */
        const float gap = 18.0f;
        const float tw = (ancho - gap * (this->op.columnas - 1)) / this->op.columnas;
        const bool botones = !this->op.botonA.empty();
        std::vector<int> orden;
        for (const Seccion& sec : this->secciones) orden.insert(orden.end(), sec.entradas.begin(), sec.entradas.end());
        // Con botones, una entrada sin X (Firmware) va sola y a todo lo ancho.
        auto ancha = [&](int i) { return botones && !(!this->op.hintX.empty() && this->tieneX(this->entradas[i].item)); };
        /* El pack que esta en la SD lleva la etiqueta INSTALADA a la derecha del
           titulo; el titulo se acomoda en lo que queda. */
        const std::string etiquetaInstalada = "menus/packs/instalada"_i18n;
        auto esInstalado = [](const Entrada& e) {
            auto* p = dynamic_cast<PackListItem*>(e.item);
            return p && p->esInstalado();
        };
        auto anchoEtiqueta = [&]() {
            nvgSave(vg);
            nvgFontFaceId(vg, this->fuenteFuerte >= 0 ? this->fuenteFuerte : regular);
            nvgFontSize(vg, 22.0f);
            nvgTextLetterSpacing(vg, 1.0f);
            const float w = nvgTextBounds(vg, 0, 0, etiquetaInstalada.c_str(), nullptr, nullptr) + 22.0f;
            nvgRestore(vg);
            return w;
        };
        auto anchoTitulo = [&](const Entrada& e, float twFila) {
            return twFila - 44.0f - (esInstalado(e) ? anchoEtiqueta() + 12.0f : 0.0f);
        };
        for (size_t base = 0, hasta = 0; base < orden.size(); base = hasta) {
            hasta = base + 1;
            if (!ancha(orden[base]))
                while (hasta < orden.size() && hasta - base < (size_t)this->op.columnas && !ancha(orden[hasta])) ++hasta;
            const float twFila = hasta - base == 1 && ancha(orden[base]) ? ancho : tw;
            bool conTitulo = false;
            float alto = 0;
            for (size_t k = base; k < hasta; ++k) {
                const Entrada& e = this->entradas[orden[k]];
                const Seccion& sec = this->secciones[e.seccion];
                if (!sec.titulo.empty() && sec.entradas.front() == orden[k]) conTitulo = true;
                nvgFontSize(vg, 20.0f);
                float h = 22.0f + lineas(vg, e.titulo, anchoTitulo(e, twFila), 2) * 26.0f;
                const bool unaLinea = hasta - base == 1 && ancha(orden[base]);
                if ((!e.item->getSubLabel().empty() && !unaLinea) || !e.item->getValue().empty()) h += 34.0f;
                nvgFontSize(vg, 15.0f);
                // A todo lo ancho la descripcion va en la misma linea del titulo.
                if (!e.desc.empty() && !(hasta - base == 1 && ancha(orden[base]))) h += lineas(vg, e.desc, twFila - 44.0f, 3) * 20.0f + 8.0f;
                // La tarjeta a todo lo ancho (Firmware) va mas compacta: entra entera.
                if (botones) h += hasta - base == 1 && ancha(orden[base]) ? 56.0f : 72.0f;
                alto = std::max(alto, h + (hasta - base == 1 && ancha(orden[base]) ? 6.0f : 16.0f));
            }
            alto = std::max(alto, 80.0f);
            const float tituloH = conTitulo ? 38.0f : 0.0f;
            for (size_t k = base; k < hasta; ++k) {
                const int indice = orden[k];
                Entrada& e = this->entradas[indice];
                const Seccion& sec = this->secciones[e.seccion];
                e.x = px + (k - base) * (twFila + gap);
                e.y = py + tituloH;
                e.w = twFila;
                e.h = alto;
                const float cx = ox + e.x, cy = oy + e.y;
                if (!sec.titulo.empty() && sec.entradas.front() == indice) {
                    /* El titulo de la seccion (Recomendado, Variantes...) como el de
                       los grupos pero en Bebas Neue: gruesa y sin pixeles, que se
                       encuentre de un vistazo. */
                    const NVGcolor colorTitulo = this->a(oqb::acentoClaro());
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, cx + 4, oy + py + 4, 5, 22, 2.5f);
                    nvgFillColor(vg, colorTitulo);
                    nvgFill(vg);
                    std::string mayus = sec.titulo;
                    for (auto& c : mayus)
                        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
                    nvgFontFaceId(vg, this->fuenteFuerte >= 0 ? this->fuenteFuerte : regular);
                    nvgFontSize(vg, 28.0f);
                    nvgTextLetterSpacing(vg, 1.0f);
                    nvgFillColor(vg, colorTitulo);
                    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                    nvgBeginPath(vg);
                    nvgText(vg, cx + 18, oy + py + 16, cortar(vg, mayus, twFila - 24).c_str(), nullptr);
                    nvgTextLetterSpacing(vg, 0.0f);
                    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                    nvgFontFaceId(vg, regular);
                }
                // El pack que esta en la SD: toda la tarjeta con el tinte del tema.
                auto* pack = dynamic_cast<PackListItem*>(e.item);
                const bool instalado = pack && pack->esInstalado();
                nvgBeginPath(vg);
                nvgRoundedRect(vg, cx, cy, twFila, alto, 16.0f);
                nvgFillColor(vg, fondo);
                nvgFill(vg);
                if (instalado) {
                    NVGcolor tinte = oqb::acentoFuerte();
                    tinte.a = oqb::oscuro() ? 0.16f : 0.12f;
                    nvgFillColor(vg, this->a(tinte));
                    nvgFill(vg);
                }
                nvgStrokeColor(vg, instalado ? this->a(oqb::acentoFuerte()) : borde);
                nvgStrokeWidth(vg, instalado ? 2.0f : 1.0f);
                nvgStroke(vg);
                if (!botones && ubicar(indice, -1, e.x, e.y, twFila, alto) && conFoco) marco(e.x, e.y, twFila, alto, 16.0f);

                float ty = cy + 20.0f;
                nvgFontSize(vg, 20.0f);
                nvgFillColor(vg, texto);
                parrafo(vg, cx + 22, ty, e.titulo, anchoTitulo(e, twFila), 26.0f, 2);
                if (instalado) {
                    // La etiqueta llena, del acento del tema, que se lea de lejos.
                    const float ew = anchoEtiqueta(), eh = 28.0f;
                    const float ex = cx + twFila - 18.0f - ew, ey = ty - 2.0f;
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, ex, ey, ew, eh, 8.0f);
                    nvgFillColor(vg, this->a(oqb::acentoFuerte()));
                    nvgFill(vg);
                    nvgFontFaceId(vg, this->fuenteFuerte >= 0 ? this->fuenteFuerte : regular);
                    nvgFontSize(vg, 22.0f);
                    nvgTextLetterSpacing(vg, 1.0f);
                    nvgFillColor(vg, this->a(oqb::oscuro() ? nvgRGB(6, 20, 12) : nvgRGB(255, 255, 255)));
                    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                    nvgBeginPath(vg);
                    nvgText(vg, ex + ew / 2, ey + eh / 2 + 1.0f, etiquetaInstalada.c_str(), nullptr);
                    nvgTextLetterSpacing(vg, 0.0f);
                    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                    nvgFontFaceId(vg, regular);
                }
                ty += lineas(vg, e.titulo, anchoTitulo(e, twFila), 2) * 26.0f + 4.0f;

                /* Tarjeta a todo lo ancho (Firmware): el subtitulo o la descripcion
                   van a la derecha del titulo, en la misma linea. */
                const bool unaLinea = hasta - base == 1 && ancha(orden[base]);
                const std::string lateral = unaLinea ? (!e.desc.empty() ? e.desc : e.item->getSubLabel()) : "";
                const std::string sub = unaLinea ? "" : e.item->getSubLabel(), valor = e.item->getValue();
                if (!sub.empty() || !valor.empty()) {
                    float sx = cx + 22;
                    if (!valor.empty()) sx += pildora(sx, ty + 14.0f, cortar(vg, valor, twFila * 0.6f), tonoDe(e.item), 14.0f) + 10.0f;
                    if (!sub.empty()) {
                        nvgFontSize(vg, 15.0f);
                        nvgFillColor(vg, tenue);
                        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                        nvgBeginPath(vg);
                        nvgText(vg, sx, ty + 14.0f, cortar(vg, sub, cx + twFila - 22 - sx).c_str(), nullptr);
                        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                    }
                    ty += 34.0f;
                }
                if (!lateral.empty()) {
                    /* Tarjeta a todo lo ancho (Firmware): la descripcion a la derecha
                       del titulo, mas chica y tenue, para que el boton suba y la
                       tarjeta entre entera en la pantalla. */
                    nvgFontSize(vg, 20.0f);
                    const float finTitulo = cx + 22 + nvgTextBounds(vg, 0, 0, e.titulo.c_str(), nullptr, nullptr) + 16.0f;
                    nvgFontSize(vg, 15.0f);
                    nvgFillColor(vg, tenue);
                    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
                    nvgBeginPath(vg);
                    nvgText(vg, finTitulo, cy + 20.0f + 19.0f, cortar(vg, lateral, cx + twFila - 22 - finTitulo).c_str(), nullptr);
                    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                }
                else if (!e.desc.empty() && !unaLinea) {
                    nvgFontSize(vg, 15.0f);
                    nvgTextLineHeight(vg, 1.3f);
                    nvgFillColor(vg, tenue);
                    parrafo(vg, cx + 22, ty + 4.0f, e.desc, twFila - 44.0f, 20.0f, 3);
                }
                if (botones) {
                    /* Los botones ocupan todo el ancho de abajo de la tarjeta; se
                       elige cada uno con la cruceta y A hace lo que dice. */
                    const bool conX = !this->op.hintX.empty() && this->tieneX(e.item);
                    const bool compacta = hasta - base == 1 && ancha(orden[base]);
                    const float bh = compacta ? 40.0f : 46.0f, bgap = 10.0f;
                    const float by = e.y + alto - (compacta ? 14.0f : 18.0f) - bh;
                    const float b1x = e.x + 18.0f, b1w = conX ? (twFila - 36.0f - bgap) / 2 : twFila - 36.0f;
                    const std::string textoA = !conX ? "menus/inicio/abrir"_i18n
                                                     : (tonoDe(e.item) == 2 && !this->op.botonAviso.empty() ? this->op.botonAviso : this->op.botonA);
                    const bool sel1 = ubicar(indice, 0, b1x, by, b1w, bh);
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, ox + b1x, oy + by, b1w, bh, 12.0f);
                    nvgFillColor(vg, this->a(oqb::esmeralda()));
                    nvgFill(vg);
                    nvgFontSize(vg, 17.0f);
                    nvgFillColor(vg, this->a(oqb::oscuro() ? nvgRGB(6, 20, 12) : nvgRGB(255, 255, 255)));
                    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                    nvgBeginPath(vg);
                    nvgText(vg, ox + b1x + b1w / 2, oy + by + bh / 2 + 1, cortar(vg, textoA, b1w - 16).c_str(), nullptr);
                    if (sel1 && conFoco) marco(b1x, by, b1w, bh, 12.0f);
                    if (conX) {
                        const float b2x = b1x + b1w + bgap;
                        const bool sel2 = ubicar(indice, 1, b2x, by, b1w, bh);
                        nvgBeginPath(vg);
                        nvgRoundedRect(vg, ox + b2x, oy + by, b1w, bh, 12.0f);
                        nvgFillColor(vg, fondoFila);
                        nvgFill(vg);
                        nvgStrokeColor(vg, borde);
                        nvgStrokeWidth(vg, 1.5f);
                        nvgStroke(vg);
                        nvgFontSize(vg, 16.0f);
                        nvgFillColor(vg, texto);
                        nvgBeginPath(vg);
                        nvgText(vg, ox + b2x + b1w / 2, oy + by + bh / 2 + 1, cortar(vg, this->op.hintX, b1w - 16).c_str(), nullptr);
                        if (sel2 && conFoco) marco(b2x, by, b1w, bh, 12.0f);
                    }
                    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                }
            }
            py += tituloH + alto + gap;
        }
    }
    else {
        /* Grupos: cajas por fila, cada una con sus entradas como filas. Se busca que
           todo entre en lo visible, sin desplazar: dos columnas con el alto de fila
           que quepa (de 56 a 44). */
        const float gap = 16.0f, cabecera = 54.0f;
        std::vector<int> conEntradas;
        for (int s = 0; s < (int)this->secciones.size(); ++s)
            if (!this->secciones[s].entradas.empty()) conEntradas.push_back(s);
        auto necesario = [&](int cols, float fh) {
            float total = 0;
            for (size_t base = 0; base < conEntradas.size(); base += cols) {
                float alto = 0;
                for (size_t k = base; k < std::min(conEntradas.size(), base + cols); ++k)
                    alto = std::max(alto, cabecera + this->secciones[conEntradas[k]].entradas.size() * fh + 10.0f);
                total += alto + gap;
            }
            return total;
        };
        const float disponible = (float)height - py - 10.0f;
        const int cols = 2;
        float filaH = 56.0f;
        while (filaH > 44.0f && necesario(cols, filaH) > disponible) filaH -= 2.0f;
        // Siempre dos columnas: con tres, los nombres no entraban a lo ancho.
        const float cw = (ancho - gap * (cols - 1)) / cols;
        for (size_t base = 0; base < conEntradas.size(); base += cols) {
            float alto = 0;
            for (size_t k = base; k < std::min(conEntradas.size(), base + cols); ++k)
                alto = std::max(alto, cabecera + this->secciones[conEntradas[k]].entradas.size() * filaH + 10.0f);
            for (size_t k = base; k < std::min(conEntradas.size(), base + cols); ++k) {
                const Seccion& sec = this->secciones[conEntradas[k]];
                const float bx = ox + px + (k - base) * (cw + gap), by = oy + py;
                nvgBeginPath(vg);
                nvgRoundedRect(vg, bx, by, cw, alto, 18.0f);
                nvgFillColor(vg, fondo);
                nvgFill(vg);
                nvgStrokeColor(vg, borde);
                nvgStrokeWidth(vg, 1.0f);
                nvgStroke(vg);
                /* El titulo del grupo, que se distinga de las entradas: una barra de
                   acento y el nombre en Upheaval (20 px), del color del grupo. */
                const NVGcolor colorTitulo = this->a(colorGrupo(conEntradas[k]));
                nvgBeginPath(vg);
                nvgRoundedRect(vg, bx + 20, by + 17, 5, 22, 2.5f);
                nvgFillColor(vg, colorTitulo);
                nvgFill(vg);
                std::string mayus = sec.titulo;
                for (auto& c : mayus)
                    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
                nvgFontFaceId(vg, this->fuente >= 0 ? this->fuente : regular);
                nvgFontSize(vg, 20.0f);
                nvgTextLetterSpacing(vg, 1.0f);
                nvgFillColor(vg, colorTitulo);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
                nvgBeginPath(vg);
                nvgText(vg, bx + 34, by + 29, cortar(vg, mayus, cw - 56).c_str(), nullptr);
                nvgTextLetterSpacing(vg, 0.0f);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                nvgFontFaceId(vg, regular);
                for (size_t r = 0; r < sec.entradas.size(); ++r) {
                    const int indice = sec.entradas[r];
                    Entrada& e = this->entradas[indice];
                    e.x = px + (k - base) * (cw + gap) + 10;
                    e.y = py + cabecera - 4.0f + r * filaH;
                    e.w = cw - 20;
                    e.h = filaH - 6;
                    const float rx = ox + e.x, ry = oy + e.y;
                    if (ubicar(indice, -1, e.x, e.y, e.w, e.h) && conFoco) {
                        nvgBeginPath(vg);
                        nvgRoundedRect(vg, rx, ry, e.w, e.h, 12.0f);
                        nvgFillColor(vg, fondoFila);
                        nvgFill(vg);
                        marco(e.x, e.y, e.w, e.h, 12.0f);
                    }
                    const std::string valor = e.item->getValue();
                    float anchoValor = 0;
                    if (!valor.empty()) {
                        nvgFontSize(vg, 14.0f);
                        const std::string v = cortar(vg, valor, e.w * 0.35f);
                        anchoValor = nvgTextBounds(vg, 0, 0, v.c_str(), nullptr, nullptr) + 16.0f;
                        nvgFillColor(vg, this->a(tonoDe(e.item) == 2 ? oqb::doradoTexto() : (tonoDe(e.item) == 1 ? oqb::verdeTexto() : ctx->theme->descriptionColor)));
                        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                        nvgBeginPath(vg);
                        nvgText(vg, rx + e.w - 12, ry + e.h / 2, v.c_str(), nullptr);
                        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                    }
                    const std::string sub = !e.item->getSubLabel().empty() ? e.item->getSubLabel() : e.desc;
                    nvgFontSize(vg, 17.0f);
                    nvgFillColor(vg, texto);
                    nvgBeginPath(vg);
                    const float arriba = sub.empty() ? (e.h - 20.0f) / 2.0f : (e.h - 38.0f) / 2.0f;
                    nvgText(vg, rx + 12, ry + arriba, cortar(vg, e.titulo, e.w - 24 - anchoValor).c_str(), nullptr);
                    if (!sub.empty()) {
                        nvgFontSize(vg, 13.0f);
                        nvgFillColor(vg, tenue);
                        nvgBeginPath(vg);
                        nvgText(vg, rx + 12, ry + arriba + 22.0f, cortar(vg, sub, e.w - 24 - anchoValor).c_str(), nullptr);
                    }
                }
            }
            py += alto + gap;
        }
    }
    nvgRestore(vg);

    // Que lo elegido quede a la vista (si cabe en la primera pantalla, sin desplazar).
    if (this->elegida >= 0 && this->elegida < (int)this->objetivos.size()) {
        const Objetivo& o = this->objetivos[this->elegida];
        const float visible = height - 20.0f;
        if (o.y + o.h <= visible)
            this->scrollObjetivo = 0.0f;
        else if (o.y - 60.0f < this->scrollObjetivo)
            this->scrollObjetivo = o.y - 60.0f;
        else if (o.y + o.h > this->scrollObjetivo + visible)
            this->scrollObjetivo = o.y + o.h - visible;
    }
}
