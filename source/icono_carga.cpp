#include "icono_carga.hpp"

#include <cmath>

// El icono de carga es el de la app PC (OQB-updater PC): la Trifuerza con
// los tres triangulos encendiendose en secuencia — poder, valor, sabiduria.
// Misma geometria del SVG (viewBox 36x32) y mismos colores: esmeralda al
// encender, musgo apagado. No gira: el movimiento es el encendido.
void drawIconoCarga(NVGcontext* vg, float cx, float cy, float t, float escala, bool completa)
{
    const float k = escala;  // 2.4 = ~86x77 px
    // Vertices de cada triangulo, como en el path del SVG de la PC.
    const float verts[3][6] = {
        {18, 1, 27, 16, 9, 16},   // arriba
        {27, 16, 36, 31, 18, 31},  // abajo derecha
        {9, 16, 18, 31, 0, 31},     // abajo izquierda
    };
    const float fase = fmodf(t, 1.5f) / 1.5f;  // ciclo de 1.5 s, como el CSS
    for (int i = 0; i < 3; ++i) {
        const bool encendida = completa || (fase >= i / 3.0f && fase < (i + 1) / 3.0f);
        nvgBeginPath(vg);
        for (int v = 0; v < 3; ++v) {
            const float px = cx + (verts[i][v * 2] - 18.0f) * k;
            const float py = cy + (verts[i][v * 2 + 1] - 16.0f) * k;
            if (v == 0) nvgMoveTo(vg, px, py); else nvgLineTo(vg, px, py);
        }
        nvgClosePath(vg);
        nvgFillColor(vg, encendida ? nvgRGB(52, 213, 131) : nvgRGB(36, 43, 34));
        nvgFill(vg);
    }
}

namespace {
    constexpr float ESCALON = 0.20f;   // entre el comienzo de un triangulo y el siguiente
    constexpr float ENCENDIDO = 0.34f; // lo que tarda cada uno en encenderse

    float suave(float x)  // ease-out cubico, 0..1
    {
        x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
        const float u = 1.0f - x;
        return 1.0f - u * u * u;
    }

    NVGcolor mezcla(NVGcolor a, NVGcolor b, float p)
    {
        return nvgRGBAf(a.r + (b.r - a.r) * p, a.g + (b.g - a.g) * p, a.b + (b.b - a.b) * p, a.a + (b.a - a.a) * p);
    }
}  // namespace

float duracionEncendido()
{
    return ESCALON * 2.0f + ENCENDIDO;
}

void drawTrifuerzaEncendido(NVGcontext* vg, float cx, float cy, float t, float escala)
{
    const float verts[3][6] = {
        {18, 1, 27, 16, 9, 16},    // arriba
        {27, 16, 36, 31, 18, 31},  // abajo derecha
        {9, 16, 18, 31, 0, 31},    // abajo izquierda
    };
    const NVGcolor apagado = nvgRGB(30, 36, 31);
    const NVGcolor esmeralda = nvgRGB(52, 213, 131);
    const NVGcolor menta = nvgRGB(150, 240, 190);

    for (int i = 0; i < 3; ++i) {
        const float p = suave((t - i * ESCALON) / ENCENDIDO);
        // Cada triangulo crece un poco desde su centro mientras se enciende.
        const float ccx = (verts[i][0] + verts[i][2] + verts[i][4]) / 3.0f;
        const float ccy = (verts[i][1] + verts[i][3] + verts[i][5]) / 3.0f;
        const float crece = 0.90f + 0.10f * p;

        nvgBeginPath(vg);
        for (int v = 0; v < 3; ++v) {
            const float vx = ccx + (verts[i][v * 2] - ccx) * crece;
            const float vy = ccy + (verts[i][v * 2 + 1] - ccy) * crece;
            const float px = cx + (vx - 18.0f) * escala;
            const float py = cy + (vy - 16.0f) * escala;
            if (v == 0) nvgMoveTo(vg, px, py); else nvgLineTo(vg, px, py);
        }
        nvgClosePath(vg);
        // Pasa por un verde menta al encenderse y se asienta en esmeralda.
        const float destello = p * (1.0f - p) * 4.0f;  // 0 -> 1 -> 0
        nvgFillColor(vg, mezcla(mezcla(apagado, esmeralda, p), menta, destello * 0.45f));
        nvgFill(vg);
        // Un halo fino que aparece y se va con el destello.
        if (destello > 0.01f) {
            nvgStrokeColor(vg, nvgRGBA(150, 240, 190, (unsigned char)(110 * destello)));
            nvgStrokeWidth(vg, 2.0f + 6.0f * destello);
            nvgLineJoin(vg, NVG_ROUND);
            nvgStroke(vg);
        }
    }
}
