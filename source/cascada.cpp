#include "cascada.hpp"

#include "oqb_theme.hpp"

namespace {

// ease-out cubico, 0..1
float suave(float x)
{
    x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    const float u = 1.0f - x;
    return 1.0f - u * u * u;
}

// El texto, recortado por el principio con "…" si no cabe: en una ruta lo
// que importa es el final (el archivo).
std::string recortar(NVGcontext* vg, const std::string& texto, float ancho)
{
    if (nvgTextBounds(vg, 0, 0, texto.c_str(), nullptr, nullptr) <= ancho)
        return texto;
    size_t desde = 0;
    std::string out;
    do {
        desde++;
        while (desde < texto.size() && (texto[desde] & 0xC0) == 0x80)
            desde++;  // no partir un caracter UTF-8
        out = "\u2026" + texto.substr(desde);
    } while (desde < texto.size() && nvgTextBounds(vg, 0, 0, out.c_str(), nullptr, nullptr) > ancho);
    return out;
}

}  // namespace

void Cascada::dibujar(NVGcontext* vg, float cx, float y, float ancho, brls::FrameContext* ctx, float alfaVista, const std::function<bool(std::string&)>& siguiente)
{
    const auto ahora = std::chrono::steady_clock::now();
    if (ahora - this->cambio >= std::chrono::milliseconds(150)) {
        std::string nueva;
        if (siguiente(nueva) && (this->cascada.empty() || this->cascada.front() != nueva)) {
            this->cascada.push_front(nueva);
            while (this->cascada.size() > 4)
                this->cascada.pop_back();
            this->cambio = ahora;
        }
    }
    if (this->cascada.empty())
        return;

    const float e = suave(std::chrono::duration<float>(ahora - this->cambio).count() / 0.28f);

    // Lugar de cada linea: -1 es de donde entra la nueva, 3 adonde sale la vieja.
    struct Lugar {
        float y, tam, alfa;
    };
    const Lugar lugares[5] = {
        {y - 14.0f, 22.0f, 0.0f},  // entrando
        {y, 22.0f, 1.0f},           // actual
        {y + 36.0f, 18.0f, 0.46f},  // anterior
        {y + 64.0f, 17.0f, 0.18f},  // la de antes
        {y + 88.0f, 16.0f, 0.0f},   // saliendo
    };

    nvgFontFaceId(vg, ctx->fontStash->regular);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    for (size_t i = 0; i < this->cascada.size(); ++i) {
        const Lugar& de = lugares[i];
        const Lugar& a = lugares[i + 1];
        const float ly = de.y + (a.y - de.y) * e;
        const float tam = de.tam + (a.tam - de.tam) * e;
        const float alfa = (de.alfa + (a.alfa - de.alfa) * e) * alfaVista;
        if (alfa < 0.01f)
            continue;

        nvgFontSize(vg, tam);
        const std::string texto = recortar(vg, this->cascada[i], ancho);
        // La actual en hueso con un punto esmeralda; las anteriores en gris verdoso.
        const bool actual = i == 0;
        // Del tema: en modo claro el hueso y el gris verdoso no se leian sobre el fondo.
        const NVGcolor base = actual ? ctx->theme->textColor : ctx->theme->descriptionColor;
        const NVGcolor color = nvgRGBAf(base.r, base.g, base.b, alfa);
        nvgFillColor(vg, color);
        nvgBeginPath(vg);
        nvgText(vg, cx, ly, texto.c_str(), nullptr);

        const float mitad = nvgTextBounds(vg, 0, 0, texto.c_str(), nullptr, nullptr) / 2.0f;
        nvgBeginPath(vg);
        nvgCircle(vg, cx - mitad - 14.0f, ly, actual ? 4.0f : 3.0f);
        const NVGcolor punto = oqb::esmeralda();
        nvgFillColor(vg, actual ? nvgRGBAf(punto.r, punto.g, punto.b, alfa) : color);
        nvgFill(vg);
    }
}
