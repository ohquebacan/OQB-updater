#include "led.hpp"
#include "worker_page.hpp"
#include "confirm_page.hpp"
#include "oqb_theme.hpp"

#include <cstring>
#include <new>

#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "constants.hpp"
#include "actividad.hpp"
#include "deep_clean.hpp"
#include "icono_carga.hpp"
#include "download.hpp"
#include "extract.hpp"
#include "main_frame.hpp"
#include "progress_event.hpp"
#include "utils.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

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

/* ProgressDisplay sin SPINNER deja su puntero `spinner` sin inicializar
   (borealis solo lo asigna con esa bandera) y su destructor lo borra igual:
   con basura en esa memoria la app se cae al destruir la pagina, por ejemplo
   al volver con B antes de descargar. borealis es un submodulo de terceros,
   asi que se crea sobre memoria en cero: el constructor no toca `spinner` y
   queda nulo, igual que `label` si faltara PERCENTAGE. */
static brls::ProgressDisplay* barraSinSpinner()
{
    void* memoria = ::operator new(sizeof(brls::ProgressDisplay));
    std::memset(memoria, 0, sizeof(brls::ProgressDisplay));
    return new (memoria) brls::ProgressDisplay(brls::ProgressDisplayFlags::PERCENTAGE);
}

WorkerPage::WorkerPage(brls::StagedAppletFrame* frame, const std::string& text, worker_func_t worker_func) : frame(frame), workerFunc(worker_func), text(text)
{
    // Sin el spinner de borealis a la izquierda: el indicador de actividad es
    // la Trifuerza de arriba.
    this->progressDisp = barraSinSpinner();
    this->progressDisp->setParent(this);

    this->label = new brls::Label(brls::LabelStyle::DIALOG, text, true);
    this->label->setHorizontalAlign(NVG_ALIGN_CENTER);
    this->label->setParent(this);

    this->button = new brls::Button(brls::ButtonStyle::REGULAR);
    this->button->setParent(this);


    this->inicioAnimacion = std::chrono::steady_clock::now();

    this->registerAction("menus/common/cancel"_i18n, brls::Key::B, [this] {
        ProgressEvent::instance().setInterupt(true);
        return true;
    });
    this->registerAction("", brls::Key::A, [this] { return true; });
    this->registerAction("", brls::Key::PLUS, [this] { return true; });
}

std::string formatLabelText(double speed, double fileSizeCurrent, double fileSizeFinal)
{
    double fileSizeCurrentMB = fileSizeCurrent / 0x100000;
    double fileSizeFinalMB = fileSizeFinal / 0x100000;
    double speedMB = speed / 0x100000;

    double timeRemaining = (fileSizeFinal - fileSizeCurrent) / speed;
    int hours = static_cast<int>(timeRemaining / 3600);
    int minutes = static_cast<int>((timeRemaining - hours * 3600) / 60);
    int seconds = static_cast<int>(timeRemaining - hours * 3600 - minutes * 60);

    std::string labelText = fmt::format("menus/worker/download_progress"_i18n, fileSizeCurrentMB, fileSizeFinalMB, speedMB);
    if (speedMB > 0) {
        std::string eta;
        if (hours > 0)
            eta += fmt::format("{}h ", hours);
        if (minutes > 0)
            eta += fmt::format("{}m ", minutes);

        eta += fmt::format("{}s", seconds);
        labelText += "\n" + fmt::format("menus/worker/time_left"_i18n, eta);
    }

    return labelText;
}

void WorkerPage::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    if (this->draw_page) {
        if (!this->workStarted) {
            this->workStarted = true;
            appletSetMediaPlaybackState(true);
            appletBeginBlockingHomeButton(0);
            ProgressEvent::instance().reset();
            workerThread = new std::thread(&WorkerPage::doWork, this);
        }
        /* Solo cuando la tarea volvio. Antes bastaba la barra llena, y hay
           tareas que siguen despues de completarla (los trucos de OQB tras la
           base, la instalacion de un NSP tras extraer): la pantalla pasaba a la
           siguiente mientras el hilo seguia, y al volver atras se congelaba
           esperandolo y la navegacion quedaba en gris. */
        else if (this->workDone.load()) {
            appletEndBlockingHomeButton();
            appletSetMediaPlaybackState(false);
            /* Un error o un cancelar no siguen con ninguna etapa (en un pack la
               ultima es reiniciar) y vuelven al punto desde el que se abrio el
               flujo. Antes: el error cerraba la app o abria el inicio, y cancelar
               apilaba una ventana principal nueva (quedaba en blanco). */
            const bool cancelado = ProgressEvent::instance().getInterupt();
            if (ProgressEvent::instance().getStatusCode() > 399 && !cancelado) {
                this->draw_page = false;
                const std::string mensaje = fmt::format("menus/errors/error_message"_i18n, util::getErrorMessage(ProgressEvent::instance().getStatusCode()));
                ProgressEvent::instance().setStatusCode(0);
                led::error();  // parpadea hasta que se cierra el aviso
                const int niveles = this->nivelesAlVolver;
                brls::Dialog* aviso = new brls::Dialog(mensaje);
                aviso->addButton("menus/common/back"_i18n, [aviso, niveles](brls::View* view) {
                    led::apagar();
                    aviso->close([niveles] { util::volverAtras(niveles); });
                });
                aviso->setCancelable(false);
                aviso->open();
                return;
            }
            if (cancelado) {
                led::apagar();
                this->draw_page = false;
                ProgressEvent::instance().setInterupt(false);
                ProgressEvent::instance().setStatusCode(0);
                util::volverAtras(this->nivelesAlVolver);
                return;
            }
            else {
                ProgressEvent::instance().setStatusCode(0);
                // Si sigue otra tarea, su pulso tapa este destello.
                led::exito();
                frame->nextStage();
            }
        }
        else {
            this->progressDisp->setProgress(ProgressEvent::instance().getStep(), ProgressEvent::instance().getMax());
            this->progressDisp->frame(ctx);
            if (ProgressEvent::instance().getTotal()) {
                this->label->setText(formatLabelText(ProgressEvent::instance().getSpeed(), ProgressEvent::instance().getNow(), ProgressEvent::instance().getTotal()));
            }
            this->label->frame(ctx);

            /* El icono de carga se dibuja con el reloj del dibujo, no con
               eventos de progreso: se mueve aunque la fase (limpieza
               profunda) no reporte nada a la barra. */
            const float tAnim = std::chrono::duration<float>(std::chrono::steady_clock::now() - this->inicioAnimacion).count();
            drawIconoCarga(vg, this->x + this->width / 2.0f, this->y + this->height / 2.0f - 145.0f, tAnim);

            this->drawCascada(vg, this->x + this->width / 2.0f, this->y + this->height / 2.0f + style->CrashFrame.buttonHeight + 28.0f, this->width * 0.8f, ctx);
        }
    }
}

void WorkerPage::layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash)
{
    this->label->setWidth(roundf((float)this->width * style->CrashFrame.labelWidth));

    this->label->setBoundaries(
        this->x + this->width / 2 - this->label->getWidth() / 2,
        this->y + (this->height - style->AppletFrame.footerHeight) / 2 - 30,
        this->label->getWidth(),
        this->label->getHeight());

    this->progressDisp->setBoundaries(
        this->x + this->width / 2 - style->CrashFrame.buttonWidth,
        this->y + this->height / 2,
        style->CrashFrame.buttonWidth * 2,
        style->CrashFrame.buttonHeight);

}

void WorkerPage::doWork()
{
    /* El registro es de esta tarea: se vacia al empezar para que la ventana
       de log nunca muestre restos de una tarea distinta (instalacion del
       pack, descarga de apps...). Las tareas que no escriben pasos dejan el
       archivo vacio y la ventana simplemente no se dibuja. */
    deep_clean::truncarLog();
    actividad::limpiar();
    // La luz del botón HOME de los mandos late mientras se trabaja; al terminar,
    // draw() la cambia a éxito, error o la apaga si se canceló.
    led::pulso();
    if (this->workerFunc)
        this->workerFunc();
    // La barra puede no haberse completado (una falla que no la llena).
    ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
    this->workDone = true;
}

brls::View* WorkerPage::getDefaultFocus()
{
    return this->button;
}

WorkerPage::~WorkerPage()
{
    if (this->workStarted && workerThread->joinable()) {
        this->workerThread->join();
        if (this->workerThread)
            delete this->workerThread;
    }
    delete this->progressDisp;
    delete this->label;
    delete this->button;
}

/* Lo que hace la app, en tres lineas: la actual arriba y, debajo, las dos
   anteriores cada vez mas tenues. Al llegar una nueva, todas bajan un lugar
   con un deslizamiento corto y la mas vieja se desvanece. Las lineas entran
   como mucho cada 150 ms, para que se alcancen a leer: lo que pase entre medio
   (cientos de archivos extraidos) se resume en el ultimo. */
void WorkerPage::drawCascada(NVGcontext* vg, float cx, float y, float ancho, brls::FrameContext* ctx)
{
    const auto ahora = std::chrono::steady_clock::now();
    if (ahora - this->cambio >= std::chrono::milliseconds(150)) {
        std::string nueva;
        if (actividad::siguiente(nueva) && (this->cascada.empty() || this->cascada.front() != nueva)) {
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
        const float alfa = (de.alfa + (a.alfa - de.alfa) * e) * this->getAlpha();
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
