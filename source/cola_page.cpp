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
    // − la cierra (si no, borealis lo usa para el contador de FPS).
    this->registerAction("", brls::Key::MINUS, [] {
        brls::Application::popView();
        return true;
    }, true);
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
    {
        std::string linea;
        while (cola::siguienteLinea(linea)) {}
    }
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

// ------------------------------------------------------- un port en la cola

InstalacionPage::InstalacionPage(int id, const std::string& nombre) : id(id), nombre(nombre)
{
    this->registerAction("menus/cola/segundo_plano"_i18n, brls::Key::B, [] {
        brls::Application::popView();
        return true;
    });
    this->registerAction("menus/common/cancel"_i18n, brls::Key::X, [this] {
        const cola::Estado e = cola::estado();
        if (e.actualId == this->id)
            cola::cancelarActual();
        else
            for (size_t i = 0; i < e.esperaIds.size(); ++i)
                if (e.esperaIds[i] == this->id) cola::quitar(i);
        brls::Application::popView();
        return true;
    });
    this->registerAction("", brls::Key::MINUS, [] { return true; }, true);
}

void InstalacionPage::abrir(int id, const std::string& nombre)
{
    brls::AppletFrame* marco = new brls::AppletFrame(true, true);
    marco->setTitle(nombre);
    marco->setContentView(new InstalacionPage(id, nombre));
    brls::Application::pushView(marco);
}

void InstalacionPage::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    const cola::Estado e = cola::estado();
    const NVGcolor tinta = this->a(ctx->theme->textColor);
    const NVGcolor gris = this->a(oqb::grisTexto());
    const NVGcolor acento = this->a(oqb::acentoFuerte());
    nvgFontFaceId(vg, ctx->fontStash->regular);
    const float cx = x + width / 2.0f, ancho = std::min(760.0f, width - 120.0f), lx = cx - ancho / 2;
    float cy = y + height / 2.0f - 70.0f;

    int antes = -1;
    for (size_t i = 0; i < e.esperaIds.size(); ++i)
        if (e.esperaIds[i] == this->id) antes = i;
    const cola::Hecho* hecho = nullptr;
    for (const auto& h : e.hechos)
        if (h.id == this->id) hecho = &h;

    if (e.actualId == this->id) {
        this->fase = 1;
        std::string titulo = e.paso;
        if (e.pasos > 1) titulo += "  (" + std::to_string(e.pasoNum) + "/" + std::to_string(e.pasos) + ")";
        texto(vg, cx, cy, titulo, 26.0f, tinta, NVG_ALIGN_CENTER);
        cy += 56.0f;
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
        nvgRoundedRect(vg, lx, cy, ancho, 12, 6);
        nvgFillColor(vg, this->a(oqb::velo(40)));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, lx, cy, ancho * frac, 12, 6);
        nvgFillColor(vg, acento);
        nvgFill(vg);
        cy += 26.0f;
        texto(vg, lx, cy, cifras, 18.0f, gris);
        texto(vg, lx + ancho, cy, std::to_string((int)std::lround(frac * 100)) + " %", 18.0f, gris, NVG_ALIGN_RIGHT);
        cy += 62.0f;
        // Las lineas en cascada, como en todas las descargas.
        this->cascada.dibujar(vg, cx, cy, ancho, ctx, this->getAlpha(), [](std::string& l) { return cola::siguienteLinea(l); });
        texto(vg, cx, y + height - 56.0f, "menus/cola/puedes_salir"_i18n, 17.0f, gris, NVG_ALIGN_CENTER);
    }
    else if (antes >= 0) {
        this->fase = 0;
        texto(vg, cx, cy, fmt::format("menus/cola/en_cola"_i18n, antes + (e.trabajando ? 1 : 0)), 26.0f, tinta, NVG_ALIGN_CENTER);
        cy += 50.0f;
        if (e.trabajando) texto(vg, cx, cy, fmt::format("menus/cola/instalando_otro"_i18n, e.actual), 18.0f, gris, NVG_ALIGN_CENTER);
        texto(vg, cx, y + height - 56.0f, "menus/cola/puedes_salir"_i18n, 17.0f, gris, NVG_ALIGN_CENTER);
    }
    else if (hecho) {
        if (this->fase != 2) {
            this->fase = 2;
            this->updateActionHint(brls::Key::B, "menus/common/back"_i18n);
            this->setActionAvailable(brls::Key::X, false);
            this->registerAction("menus/common/ok"_i18n, brls::Key::A, [] {
                brls::Application::popView();
                return true;
            });
        }
        const bool bien = hecho->error.empty() && !hecho->cancelado;
        const std::string t = bien ? fmt::format("menus/cola/listo"_i18n, this->nombre)
                                   : hecho->cancelado ? "menus/cola/cancelado"_i18n
                                                      : hecho->error;
        texto(vg, cx, cy, t, 26.0f, bien ? this->a(oqb::verdeTexto()) : this->a(oqb::rojoTexto()), NVG_ALIGN_CENTER);
    }
}
