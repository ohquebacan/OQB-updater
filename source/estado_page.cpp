#include "estado_page.hpp"

#include <algorithm>
#include <cmath>

#include "cliente_page.hpp"
#include "constants.hpp"
#include "crash_report.hpp"
#include "diagnostics_page.hpp"
#include "donar_page.hpp"
#include "iconos_oqb.hpp"
#include "oqb_theme.hpp"
#include "protection_page.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {

    std::string cortar(NVGcontext* vg, const std::string& texto, float ancho)
    {
        if (ancho <= 0) return "";
        if (nvgTextBounds(vg, 0, 0, texto.c_str(), nullptr, nullptr) <= ancho) return texto;
        std::string t = texto;
        while (!t.empty()) {
            t.pop_back();
            while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
            const std::string prueba = t + "…";
            if (nvgTextBounds(vg, 0, 0, prueba.c_str(), nullptr, nullptr) <= ancho) return prueba;
        }
        return "";
    }

    // Un parrafo partido en lineas de `ancho`, hasta `maximo` lineas.
    void parrafo(NVGcontext* vg, float x, float y, const std::string& t, float ancho, float alto, int maximo)
    {
        const char* p = t.c_str();
        const char* fin = p + t.size();
        NVGtextRow fila[1];
        for (int i = 0; i < maximo && p < fin; ++i) {
            if (nvgTextBreakLines(vg, p, fin, ancho, fila, 1) <= 0) break;
            nvgBeginPath(vg);
            nvgText(vg, x, y + i * alto, fila[0].start, fila[0].end);
            p = fila[0].next;
        }
    }

    void pildora(NVGcontext* vg, float derecha, float cy, const std::string& texto, NVGcolor color, NVGcolor fondo)
    {
        nvgFontSize(vg, 16.0f);
        const float w = nvgTextBounds(vg, 0, 0, texto.c_str(), nullptr, nullptr) + 30.0f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, derecha - w, cy - 17, w, 34, 17);
        nvgFillColor(vg, fondo);
        nvgFill(vg);
        nvgFillColor(vg, color);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgBeginPath(vg);
        nvgText(vg, derecha - w / 2, cy + 1, texto.c_str(), nullptr);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    }

    NVGcolor conAlfa(NVGcolor c, float a)
    {
        c.a = a;
        return c;
    }

}  // namespace

EstadoView::EstadoView(const std::string& donationUrl, const std::vector<protection::Check>& checks)
    : donacion(donationUrl), checks(checks), cliente(cliente::cargar())
{
    // El diagnostico, resumido: cuantos informes hay y el ultimo.
    {
        const auto grupos = crash::group(crash::readAll(std::vector<std::string>{CRASH_REPORTS_PATH, FATAL_REPORTS_PATH}));
        this->programasConError = (int)grupos.size();
        for (const auto& g : grupos) this->errores += g.count;
        if (!grupos.empty()) {
            this->ultimoNombre = crashProgramName(grupos.front());
            this->ultimoCodigo = grupos.front().lastResult;
            this->ultimaFecha = crash::date(grupos.front().lastTimestamp);
        }
    }
    this->objetivos = {Protecciones, Diagnostico, Cliente};
    if (!this->donacion.empty()) {
        this->objetivos.push_back(Apoyo);
        this->qr = new QrCodeView(this->donacion);
        this->qr->setParent(this);
    }
    this->objetivos.push_back(Descargo);

    this->registerAction("", brls::Key::DUP, [this] { return this->mover(0, -1); }, true);
    this->registerAction("", brls::Key::DDOWN, [this] { return this->mover(0, 1); }, true);
    this->registerAction("", brls::Key::DLEFT, [this] { return this->mover(-1, 0); }, true);
    this->registerAction("", brls::Key::DRIGHT, [this] { return this->mover(1, 0); }, true);
    this->registerAction("menus/inicio/abrir"_i18n, brls::Key::A, [this] {
        this->abrir();
        return true;
    });
}

EstadoView::~EstadoView()
{
    delete this->qr;
}

bool EstadoView::mover(int dx, int dy)
{
    const Objetivo actual = this->objetivos[this->elegido];
    auto ir = [this](Objetivo o) {
        for (int i = 0; i < (int)this->objetivos.size(); ++i)
            if (this->objetivos[i] == o) {
                this->elegido = i;
                return true;
            }
        return false;
    };
    // Protecciones arriba; Cliente y Apoyo en la misma fila; Descargo abajo.
    // De arriba abajo: Protecciones, Diagnostico, Cliente | Apoyo, Descargo.
    if (dy > 0) {
        if (actual == Protecciones) return ir(Diagnostico);
        if (actual == Diagnostico) return ir(Cliente);
        if (actual == Cliente || actual == Apoyo) return ir(Descargo);
    }
    else if (dy < 0) {
        if (actual == Descargo) return ir(Cliente);
        if (actual == Cliente || actual == Apoyo) return ir(Diagnostico);
        if (actual == Diagnostico) return ir(Protecciones);
    }
    else if (dx > 0 && actual == Cliente) {
        return ir(Apoyo);
    }
    else if (dx < 0 && actual == Apoyo) {
        return ir(Cliente);
    }
    return true;
}

void EstadoView::abrir()
{
    switch (this->objetivos[this->elegido]) {
        case Protecciones:
            brls::PopupFrame::open("menus/tools/protection"_i18n, new ProtectionPage(), "menus/tools/protection_desc"_i18n, "");
            break;
        case Cliente:
            brls::PopupFrame::open("menus/cliente/titulo"_i18n, new ClientePage([this](const cliente::Estado& e) { this->cliente = e; }), "", "");
            break;
        case Apoyo:
            brls::Application::pushView(new DonarPage(this->donacion));
            break;
        case Diagnostico:
            brls::PopupFrame::open("menus/tools/diagnostics"_i18n, new DiagnosticsPage(), "menus/tools/diagnostics_desc"_i18n, "");
            break;
        case Descargo: {
            brls::List* l = new brls::List();
            brls::Label* t = new brls::Label(brls::LabelStyle::REGULAR,
                                             "menus/about/title"_i18n + "\nOH! QUÉ BACÁN\n\n" + "menus/about/copyright"_i18n + "\n" + "menus/about/disclaimers"_i18n,
                                             true);
            t->setFontSize(brls::Application::getStyle()->Label.descriptionFontSize + 2);
            l->addView(t);
            brls::ListItem* volver = new brls::ListItem("menus/common/back"_i18n);
            volver->getClickEvent()->subscribe([](brls::View* v) { brls::Application::popView(); });
            l->addView(volver);
            brls::AppletFrame* frame = new brls::AppletFrame(true, true);
            frame->setContentView(l);
            brls::PopupFrame::open("menus/about/disclaimers_title"_i18n, frame, "", "");
            break;
        }
    }
}

void EstadoView::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    if (!this->fuenteIntentada) {
        this->fuenteIntentada = true;
        this->fuente = nvgFindFont(vg, "upheaval");
        if (this->fuente < 0) this->fuente = nvgCreateFont(vg, "upheaval", "romfs:/fonts/upheavtt.ttf");
    }
    const int regular = ctx->fontStash->regular;
    const NVGcolor texto = this->a(ctx->theme->textColor);
    const NVGcolor tenue = this->a(ctx->theme->descriptionColor);
    const NVGcolor tarjeta = this->a(oqb::velo(oqb::oscuro() ? 12 : 10));
    const NVGcolor foco = this->a(ctx->theme->highlightColor1);
    const bool conFoco = this->isFocused();
    const Objetivo elegido = this->objetivos[this->elegido];
    auto marco = [&](Objetivo o, float bx, float by, float bw, float bh, float radio) {
        if (!conFoco || o != elegido) return;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, bx - 3, by - 3, bw + 6, bh + 6, radio + 3);
        nvgStrokeColor(vg, foco);
        nvgStrokeWidth(vg, 3.0f);
        nvgStroke(vg);
    };

    const float px = x + 60.0f, ancho = width - 120.0f;
    float py = y + 26.0f;

    // Titulo.
    nvgFontFaceId(vg, this->fuente >= 0 ? this->fuente : regular);
    nvgFontSize(vg, 40.0f);
    nvgTextLetterSpacing(vg, 1.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, texto);
    nvgBeginPath(vg);
    nvgText(vg, px, py, "menus/estado/titulo"_i18n.c_str(), nullptr);
    nvgTextLetterSpacing(vg, 0.0f);
    nvgFontFaceId(vg, regular);
    nvgFontSize(vg, 17.0f);
    nvgFillColor(vg, tenue);
    nvgBeginPath(vg);
    nvgText(vg, px, py + 46.0f, "menus/estado/subtitulo"_i18n.c_str(), nullptr);
    py += 84.0f;

    // Protecciones: resumen y cada comprobacion.
    {
        const bool falla = protection::anyFailed(this->checks);
        const bool nextendo = protection::failedOnlyByNextendo(this->checks);
        const NVGcolor tono = nextendo ? (oqb::oscuro() ? nvgRGB(187, 168, 255) : nvgRGB(75, 50, 168))
                              : falla  ? oqb::rojoTexto()
                                       : oqb::verdeTexto();
        const size_t bien = std::count_if(this->checks.begin(), this->checks.end(),
                                          [](const protection::Check& c) { return c.status == protection::Status::Ok; });
        const size_t filas = (this->checks.size() + 1) / 2;
        const float bh = 92.0f + filas * 30.0f + 10.0f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, px, py, ancho, bh, 16.0f);
        nvgFillColor(vg, tarjeta);
        nvgFill(vg);
        marco(Protecciones, px, py, ancho, bh, 16.0f);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, px + 26, py + 24, 56, 56, 14.0f);
        nvgFillColor(vg, this->a(conAlfa(tono, 0.16f)));
        nvgFill(vg);
        iconos::dibujar(vg, iconos::Icono::Escudo, px + 54, py + 52, 30.0f, this->a(tono));

        nvgFontSize(vg, 24.0f);
        nvgFillColor(vg, texto);
        nvgBeginPath(vg);
        nvgText(vg, px + 100, py + 24, (nextendo ? "menus/estado/prot_nextendo"_i18n : falla ? "menus/estado/prot_falla"_i18n : "menus/estado/prot_ok"_i18n).c_str(), nullptr);
        nvgFontSize(vg, 16.0f);
        nvgFillColor(vg, tenue);
        nvgBeginPath(vg);
        nvgText(vg, px + 100, py + 58, cortar(vg, nextendo ? "menus/estado/prot_nextendo_sub"_i18n : falla ? "menus/estado/prot_falla_sub"_i18n : "menus/estado/prot_ok_sub"_i18n, ancho - 330).c_str(), nullptr);
        pildora(vg, px + ancho - 26, py + 44, fmt::format("menus/estado/bien"_i18n, bien, this->checks.size()), this->a(tono), this->a(conAlfa(tono, 0.16f)));

        const float cw = (ancho - 52 - 24) / 2;
        for (size_t i = 0; i < this->checks.size(); ++i) {
            const protection::Check& c = this->checks[i];
            const float cx = px + 26 + (i % 2) * (cw + 24), cy = py + 94 + (i / 2) * 30;
            const NVGcolor punto = c.status == protection::Status::Ok        ? oqb::esmeralda()
                                   : c.byNextendo                           ? (oqb::oscuro() ? nvgRGB(187, 168, 255) : nvgRGB(75, 50, 168))
                                   : c.status == protection::Status::Warning ? oqb::doradoTexto()
                                                                            : oqb::rojoTexto();
            nvgBeginPath(vg);
            nvgCircle(vg, cx + 6, cy + 11, 6);
            nvgFillColor(vg, this->a(punto));
            nvgFill(vg);
            nvgFontSize(vg, 16.0f);
            nvgFillColor(vg, texto);
            nvgBeginPath(vg);
            nvgText(vg, cx + 22, cy + 2, cortar(vg, c.name, cw - 22).c_str(), nullptr);
        }
        py += bh + 14.0f;
    }

    // Diagnostico de errores: a todo lo ancho, como las protecciones.
    {
        const bool hay = this->errores > 0;
        const NVGcolor tono = hay ? oqb::doradoTexto() : oqb::verdeTexto();
        const float bh = 96.0f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, px, py, ancho, bh, 16.0f);
        nvgFillColor(vg, tarjeta);
        nvgFill(vg);
        marco(Diagnostico, px, py, ancho, bh, 16.0f);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, px + 26, py + 20, 56, 56, 14.0f);
        nvgFillColor(vg, this->a(conAlfa(tono, 0.16f)));
        nvgFill(vg);
        iconos::dibujar(vg, iconos::Icono::Herramientas, px + 54, py + 48, 28.0f, this->a(tono));
        nvgFontSize(vg, 22.0f);
        nvgFillColor(vg, texto);
        nvgBeginPath(vg);
        nvgText(vg, px + 100, py + 20, "menus/tools/diagnostics"_i18n.c_str(), nullptr);
        nvgFontSize(vg, 16.0f);
        nvgFillColor(vg, tenue);
        nvgBeginPath(vg);
        const std::string sub = !hay ? "menus/estado/diag_nada"_i18n
                                     : fmt::format("menus/estado/diag_ultimo"_i18n, this->ultimoNombre,
                                                   this->ultimoCodigo.empty() ? "?" : this->ultimoCodigo, this->ultimaFecha);
        nvgText(vg, px + 100, py + 54, cortar(vg, sub, ancho - 330).c_str(), nullptr);
        const std::string pill = !hay ? "menus/estado/diag_limpio"_i18n
                                 : this->errores == 1 ? "menus/estado/diag_uno"_i18n
                                                      : fmt::format("menus/estado/diag_varios"_i18n, this->errores);
        pildora(vg, px + ancho - 26, py + bh / 2, pill, this->a(tono), this->a(conAlfa(tono, 0.16f)));
        py += bh + 14.0f;
    }

    // Cliente OQB y apoyo, lado a lado.
    {
        const bool conApoyo = this->qr != nullptr;
        const float gap = 14.0f, bh = 140.0f, bw = conApoyo ? (ancho - gap) / 2 : ancho;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, px, py, bw, bh, 16.0f);
        nvgFillColor(vg, tarjeta);
        nvgFill(vg);
        marco(Cliente, px, py, bw, bh, 16.0f);
        nvgFontSize(vg, 21.0f);
        nvgFillColor(vg, texto);
        nvgBeginPath(vg);
        nvgText(vg, px + 24, py + 20, "menus/cliente/titulo"_i18n.c_str(), nullptr);
        const NVGcolor tonoCliente = this->cliente.valido ? oqb::verdeTexto() : oqb::doradoTexto();
        nvgFontSize(vg, 16.0f);
        const std::string estadoTxt = this->cliente.valido ? "menus/cliente/registrado"_i18n : "menus/cliente/no_registrado"_i18n;
        const float pw = nvgTextBounds(vg, 0, 0, estadoTxt.c_str(), nullptr, nullptr) + 30.0f;
        pildora(vg, px + 24 + pw, py + 68, estadoTxt, this->a(tonoCliente), this->a(conAlfa(tonoCliente, 0.16f)));
        nvgFontSize(vg, 15.0f);
        nvgFillColor(vg, tenue);
        parrafo(vg, px + 24, py + 96,
                this->cliente.valido ? fmt::format("menus/cliente/registrado_sub"_i18n, this->cliente.nombre) : "menus/cliente/no_registrado_sub"_i18n,
                bw - 48, 21.0f, 2);

        if (conApoyo) {
            const float ax = px + bw + gap;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, ax, py, bw, bh, 16.0f);
            nvgFillColor(vg, tarjeta);
            nvgFill(vg);
            marco(Apoyo, ax, py, bw, bh, 16.0f);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, ax + 14, py + 14, 112, 112, 10.0f);
            nvgFillColor(vg, nvgRGB(255, 255, 255));
            nvgFill(vg);
            this->qr->setBoundaries(ax + 18, py + 18, 104, 104);
            this->qr->frame(ctx);
            nvgFontSize(vg, 21.0f);
            nvgFillColor(vg, texto);
            nvgBeginPath(vg);
            nvgText(vg, ax + 144, py + 18, "menus/donar/support_title"_i18n.c_str(), nullptr);
            nvgFontSize(vg, 15.0f);
            nvgFillColor(vg, tenue);
            parrafo(vg, ax + 144, py + 52, "menus/donar/support_text"_i18n, bw - 166, 20.0f, 4);
        }
        py += bh + 14.0f;
    }

    // Abajo, discretos: el diagnostico de errores y el descargo.
    {
        nvgFontSize(vg, 16.0f);
        float lx = px + 4;
        for (const auto& [o, t] : {std::pair<Objetivo, std::string>{Descargo, "menus/about/disclaimers_title"_i18n + "  ›"}}) {
            const float w = nvgTextBounds(vg, 0, 0, t.c_str(), nullptr, nullptr);
            nvgFillColor(vg, o == Diagnostico ? texto : tenue);
            nvgBeginPath(vg);
            nvgText(vg, lx, py + 4, t.c_str(), nullptr);
            marco(o, lx - 10, py - 4, w + 20, 30, 8.0f);
            lx += w + 48;
        }
    }
}

EstadoFrame::EstadoFrame(const std::string& donationUrl, const std::vector<protection::Check>& checks)
    : brls::AppletFrame(false, false)
{
    this->setContentView(new EstadoView(donationUrl, checks));
}

/* Sin la cabecera de borealis, como el menu principal: la altura de la cabecera
   es global del estilo, asi que se pone en 0 solo mientras se acomoda y se dibuja. */
void EstadoFrame::layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash)
{
    const unsigned cabecera = style->AppletFrame.headerHeightRegular;
    style->AppletFrame.headerHeightRegular = 0;
    brls::AppletFrame::layout(vg, style, stash);
    style->AppletFrame.headerHeightRegular = cabecera;
}

void EstadoFrame::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    const unsigned cabecera = style->AppletFrame.headerHeightRegular;
    style->AppletFrame.headerHeightRegular = 0;
    brls::AppletFrame::draw(vg, x, y, width, height, style, ctx);
    style->AppletFrame.headerHeightRegular = cabecera;
}
