#include "worker_page.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "constants.hpp"
#include "download.hpp"
#include "extract.hpp"
#include "main_frame.hpp"
#include "progress_event.hpp"
#include "utils.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {

// El icono de carga es el de la app PC (OQB-updater PC): la Trifuerza con
// los tres triangulos encendiendose en secuencia — poder, valor, sabiduria.
// Misma geometria del SVG (viewBox 36x32) y mismos colores: esmeralda al
// encender, musgo apagado. No gira: el movimiento es el encendido.
void drawIconoCarga(NVGcontext* vg, float cx, float cy, float t)
{
    const float k = 2.4f;  // escala del viewBox 36x32 (~86x77 px)
    // Vertices de cada triangulo, como en el path del SVG de la PC.
    const float verts[3][6] = {
        {18, 1, 27, 16, 9, 16},   // arriba
        {27, 16, 36, 31, 18, 31},  // abajo derecha
        {9, 16, 18, 31, 0, 31},     // abajo izquierda
    };
    const float fase = fmodf(t, 1.5f) / 1.5f;  // ciclo de 1.5 s, como el CSS
    for (int i = 0; i < 3; ++i) {
        const bool encendida = fase >= i / 3.0f && fase < (i + 1) / 3.0f;
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

// Las ultimas lineas del registro de la instalacion, para la ventana de log.
std::string colaDelLog()
{
    std::ifstream f(std::string(CONFIG_PATH) + "deep_clean.log");
    if (!f.good())
        return "";
    std::vector<std::string> lineas;
    std::string l;
    while (std::getline(f, l))
        lineas.push_back(l);
    std::string out;
    const size_t desde = lineas.size() > 9 ? lineas.size() - 9 : 0;
    for (size_t i = desde; i < lineas.size(); ++i) {
        out += lineas[i];
        if (i + 1 < lineas.size())
            out += "\n";
    }
    return out;
}

}  // namespace

WorkerPage::WorkerPage(brls::StagedAppletFrame* frame, const std::string& text, worker_func_t worker_func) : frame(frame), workerFunc(worker_func), text(text)
{
    this->progressDisp = new brls::ProgressDisplay();
    this->progressDisp->setParent(this);

    this->label = new brls::Label(brls::LabelStyle::DIALOG, text, true);
    this->label->setHorizontalAlign(NVG_ALIGN_CENTER);
    this->label->setParent(this);

    this->button = new brls::Button(brls::ButtonStyle::REGULAR);
    this->button->setParent(this);

    this->logLabel = new brls::Label(brls::LabelStyle::SMALL, "", true);
    this->logLabel->setHorizontalAlign(NVG_ALIGN_LEFT);
    this->logLabel->setColor(nvgRGBA(170, 175, 190, 255));
    this->logLabel->setParent(this);

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
        else if (ProgressEvent::instance().finished()) {
            appletEndBlockingHomeButton();
            appletSetMediaPlaybackState(false);
            if (ProgressEvent::instance().getStatusCode() > 399) {
                this->draw_page = false;
                brls::Application::crash(fmt::format("menus/errors/error_message"_i18n, util::getErrorMessage(ProgressEvent::instance().getStatusCode())));
            }
            if (ProgressEvent::instance().getInterupt()) {
                brls::Application::pushView(new MainFrame());
            }
            else {
                ProgressEvent::instance().setStatusCode(0);
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

            const auto ahora = std::chrono::steady_clock::now();
            if (ahora - this->ultimaLecturaLog > std::chrono::milliseconds(250)) {
                this->ultimaLecturaLog = ahora;
                this->logLabel->setText(colaDelLog());
            }
            if (!this->logLabel->getText().empty()) {
                /* Panel con fondo: el log no flota suelto sobre la interfaz. */
                const float px = this->logLabel->getX() - 18.0f;
                const float py = this->logLabel->getY() - 12.0f;
                const float pw = this->logLabel->getWidth() + 36.0f;
                const float ph = this->logLabel->getHeight() + 24.0f;
                nvgBeginPath(vg);
                nvgRoundedRect(vg, px, py, pw, ph, 10.0f);
                nvgFillColor(vg, nvgRGBA(14, 18, 14, 216));
                nvgFill(vg);
                nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 16));
                nvgStrokeWidth(vg, 1.5f);
                nvgStroke(vg);
                this->logLabel->frame(ctx);
            }
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

    this->logLabel->setBoundaries(
        this->x + this->width * 0.08f,
        this->y + this->height / 2 + style->CrashFrame.buttonHeight + 20,
        this->width * 0.84f,
        170);
}

void WorkerPage::doWork()
{
    if (this->workerFunc)
        this->workerFunc();
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
    delete this->logLabel;
    delete this->button;
}