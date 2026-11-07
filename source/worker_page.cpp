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

// Un triangulo equilatero con el centroide en (cx, cy), apuntando arriba.
void triangulo(NVGcontext* vg, float cx, float cy, float lado)
{
    const float h = lado * 0.8660254f;
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx, cy - 2.0f * h / 3.0f);
    nvgLineTo(vg, cx - lado / 2.0f, cy + h / 3.0f);
    nvgLineTo(vg, cx + lado / 2.0f, cy + h / 3.0f);
    nvgClosePath(vg);
}

// La Trifuerza: tres triangulos de oro girando lentamente y pulsando. Se
// dibuja con el reloj del dibujo, no con eventos de progreso, asi que nunca
// se congela aunque el trabajo de fondo no reporte nada (fase de limpieza).
void drawTrifuerza(NVGcontext* vg, float cx, float cy, float t)
{
    const float lado = 26.0f;
    const float R = lado * 0.5773503f;  // centroide grande -> de cada triangulo
    const float giro = t * 40.0f;       // grados por segundo
    const float pulso = 0.92f + 0.08f * sinf(t * 2.2f);

    nvgSave(vg);
    nvgTranslate(vg, cx, cy);
    nvgRotate(vg, (giro - 90.0f) * NVG_PI / 180.0f);
    nvgScale(vg, pulso, pulso);

    for (int i = 0; i < 3; ++i) {
        const float ang = i * 2.0f * NVG_PI / 3.0f - NVG_PI / 2.0f;
        triangulo(vg, R * cosf(ang), R * sinf(ang), lado);
    }
    NVGpaint oro = nvgLinearGradient(vg, 0, -R - lado, 0, R + lado,
        nvgRGBA(255, 216, 92, 255), nvgRGBA(214, 148, 18, 255));
    nvgFillPaint(vg, oro);
    nvgFill(vg);
    nvgStrokeColor(vg, nvgRGBA(92, 60, 6, 255));
    nvgStrokeWidth(vg, 2.0f);
    nvgStroke(vg);
    nvgRestore(vg);
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
    this->logLabel->setHorizontalAlign(NVG_ALIGN_CENTER);
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

            /* La Trifuerza gira siempre: su animacion depende solo del reloj
               del dibujo, asi que muestra vida incluso en la fase de limpieza
               profunda, que no reporta progreso a la barra. */
            const float tAnim = std::chrono::duration<float>(std::chrono::steady_clock::now() - this->inicioAnimacion).count();
            drawTrifuerza(vg, this->x + this->width / 2.0f, this->y + this->height / 2.0f - 72.0f, tAnim);

            const auto ahora = std::chrono::steady_clock::now();
            if (ahora - this->ultimaLecturaLog > std::chrono::milliseconds(250)) {
                this->ultimaLecturaLog = ahora;
                this->logLabel->setText(colaDelLog());
            }
            this->logLabel->frame(ctx);
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