#include "cola_page.hpp"

#include <cmath>

#include "cola.hpp"
#include "oqb_theme.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {

    std::string mb(double b)
    {
        char t[32];
        if (b >= 1024.0 * 1024 * 1024)
            snprintf(t, sizeof(t), "%.2f GB", b / (1024.0 * 1024 * 1024));
        else
            snprintf(t, sizeof(t), "%.1f MB", b / (1024.0 * 1024));
        return t;
    }

    void texto(NVGcontext* vg, float x, float y, const std::string& s, float tam, NVGcolor c, int alinear = NVG_ALIGN_LEFT)
    {
        nvgFontSize(vg, tam);
        nvgFillColor(vg, c);
        nvgTextAlign(vg, alinear | NVG_ALIGN_TOP);
        nvgBeginPath(vg);
        nvgText(vg, x, y, s.c_str(), nullptr);
    }

}  // namespace

ColaPage::ColaPage()
{
    this->registerAction("", brls::Key::DUP, [this] { this->mover(-1); return true; }, true);
    this->registerAction("", brls::Key::DDOWN, [this] { this->mover(1); return true; }, true);
    this->registerAction("menus/cola/quitar"_i18n, brls::Key::X, [this] {
        const cola::Estado e = cola::estado();
        if (this->cursor == 0) {
            if (e.trabajando) cola::cancelarActual();
        }
        else
            cola::quitar(this->cursor - 1);
        this->mover(0);
        return true;
    });
    this->registerAction("menus/cola/subir"_i18n, brls::Key::L, [this] {
        if (this->cursor > 1) {
            cola::mover(this->cursor - 1, -1);
            --this->cursor;
        }
        return true;
    });
    this->registerAction("menus/cola/bajar"_i18n, brls::Key::R, [this] {
        const cola::Estado e = cola::estado();
        if (this->cursor >= 1 && this->cursor < (int)e.espera.size()) {
            cola::mover(this->cursor - 1, 1);
            ++this->cursor;
        }
        return true;
    });
    this->registerAction("menus/cola/limpiar"_i18n, brls::Key::Y, [] {
        cola::limpiarHechos();
        return true;
    });
}

void ColaPage::mover(int d)
{
    const cola::Estado e = cola::estado();
    this->cursor = std::max(0, std::min((int)e.espera.size(), this->cursor + d));
}

void ColaPage::abrir()
{
    brls::AppletFrame* marco = new brls::AppletFrame(false, false);
    marco->setContentView(new ColaPage());
    brls::PopupFrame::open("menus/cola/titulo"_i18n, marco, "", "");
}

void ColaPage::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    const cola::Estado e = cola::estado();
    if (this->cursor > (int)e.espera.size()) this->cursor = e.espera.size();

    const float lx = x + 50.0f, ancho = width - 100.0f;
    float ly = y + 30.0f;
    const NVGcolor tinta = this->a(ctx->theme->textColor);
    const NVGcolor gris = this->a(oqb::grisTexto());
    const NVGcolor acento = this->a(oqb::acentoFuerte());
    nvgFontFaceId(vg, ctx->fontStash->regular);

    const auto marco = [&](bool sel, float alto) {
        if (!sel) return;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, lx - 14, ly - 8, ancho + 28, alto, 10.0f);
        nvgStrokeColor(vg, acento);
        nvgStrokeWidth(vg, 3.0f);
        nvgStroke(vg);
    };

    // Lo de ahora.
    texto(vg, lx, ly, "menus/cola/ahora"_i18n, 18.0f, gris);
    ly += 30.0f;
    if (e.trabajando) {
        marco(this->cursor == 0, 108.0f);
        texto(vg, lx, ly, e.actual, 24.0f, tinta);
        std::string paso = e.paso;
        if (e.pasos > 1) paso += "  ·  " + std::to_string(e.pasoNum) + "/" + std::to_string(e.pasos);
        texto(vg, lx + ancho, ly + 4, paso, 18.0f, gris, NVG_ALIGN_RIGHT);
        ly += 38.0f;

        double frac = 0;
        std::string cifras;
        if (e.total > 0) {
            frac = e.ahora / e.total;
            cifras = mb(e.ahora) + " / " + mb(e.total);
            if (e.velocidad > 0) cifras += "  ·  " + mb(e.velocidad) + "/s";
        }
        else if (e.max > 0)
            frac = (double)e.step / e.max;
        frac = std::max(0.0, std::min(1.0, frac));
        nvgBeginPath(vg);
        nvgRoundedRect(vg, lx, ly, ancho, 10, 5);
        nvgFillColor(vg, this->a(oqb::velo(40)));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, lx, ly, ancho * frac, 10, 5);
        nvgFillColor(vg, acento);
        nvgFill(vg);
        ly += 20.0f;
        texto(vg, lx, ly, e.detalle.empty() ? cifras : e.detalle, 16.0f, gris);
        texto(vg, lx + ancho, ly, std::to_string((int)std::lround(frac * 100)) + " %", 16.0f, gris, NVG_ALIGN_RIGHT);
        ly += 50.0f;
    }
    else {
        texto(vg, lx, ly, "menus/cola/nada"_i18n, 20.0f, gris);
        ly += 50.0f;
    }

    // Lo que espera.
    texto(vg, lx, ly, fmt::format("menus/cola/en_espera"_i18n, e.espera.size()), 18.0f, gris);
    ly += 32.0f;
    for (size_t i = 0; i < e.espera.size() && ly < y + height - 160; ++i) {
        marco(this->cursor == (int)i + 1, 40.0f);
        texto(vg, lx, ly, std::to_string(i + 1) + ".  " + e.espera[i], 20.0f, tinta);
        ly += 44.0f;
    }
    if (e.espera.empty()) ly += 4.0f;
    ly += 16.0f;

    // Lo ultimo terminado.
    if (!e.hechos.empty()) {
        texto(vg, lx, ly, "menus/cola/terminados"_i18n, 18.0f, gris);
        ly += 32.0f;
        for (const auto& h : e.hechos) {
            if (ly > y + height - 40) break;
            const bool bien = h.error.empty() && !h.cancelado;
            const std::string marca = bien ? "✓  " : "✗  ";
            const std::string estado = h.cancelado ? "menus/cola/cancelado"_i18n : h.error;
            texto(vg, lx, ly, marca + h.nombre, 18.0f, bien ? this->a(oqb::verdeTexto()) : this->a(oqb::rojoTexto()));
            if (!estado.empty()) texto(vg, lx + ancho, ly, estado, 16.0f, gris, NVG_ALIGN_RIGHT);
            ly += 30.0f;
        }
    }
}
