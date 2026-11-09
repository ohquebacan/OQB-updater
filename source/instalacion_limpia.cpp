#include "instalacion_limpia.hpp"

#include <borealis.hpp>
#include <GLFW/glfw3.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

#include "confirm_page.hpp"
#include "constants.hpp"
#include "deep_clean.hpp"
#include "download.hpp"
#include "main_frame.hpp"
#include "pack_list_item.hpp"
#include "pack_version.hpp"
#include "progress_event.hpp"
#include "oqb_theme.hpp"
#include "utils.hpp"
#include "worker_page.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;
using deep_clean::estricta::Categoria;

namespace {

    struct Estado {
        std::string nombre, url, clave, fecha;
        deep_clean::estricta::Plan plan;
        deep_clean::estricta::Opciones op;
        bool erista = true;
    };

    std::string tam(uint64_t b)
    {
        std::string s;
        if (b >= (1ull << 30))
            s = fmt::format("{:.1f} GB", b / 1073741824.0);
        else if (b >= (1ull << 20))
            s = fmt::format("{:.0f} MB", b / 1048576.0);
        else
            s = fmt::format("{:.0f} KB", b / 1024.0);
        const std::string coma = "menus/limpia/decimal"_i18n;
        if (coma == ",") {
            const auto p = s.find('.');
            if (p != std::string::npos) s[p] = ',';
        }
        return s;
    }

    /* Cancelar en la vista previa o al confirmar: se cierran los dos marcos (el de
       la vista previa y el del pack elegido) y se vuelve a Herramientas, sin tocar
       la SD. Antes se abria otra ventana principal encima. */
    void volverAlInicio()
    {
        util::volverAtras(2);
    }

    // Los primeros nombres de una categoria, para que se sepa que hay adentro.
    std::string ejemplos(const deep_clean::estricta::Plan& plan, Categoria cat)
    {
        std::string out;
        int n = 0;
        for (const auto& e : plan.elementos) {
            if (e.cat != cat) continue;
            if (n == 3) {
                out += ", …";
                break;
            }
            const auto barra = e.ruta.find_last_of('/');
            out += (n ? ", " : "") + (barra == std::string::npos ? e.ruta : e.ruta.substr(barra + 1));
            ++n;
        }
        return out;
    }

    std::string subtitulo(const deep_clean::estricta::Plan& plan, Categoria cat)
    {
        return fmt::format("menus/limpia/cuenta"_i18n, plan.cuenta[cat], tam(plan.bytes[cat]), ejemplos(plan, cat));
    }

    /* Arriba de la vista previa: cuanto se borra y cuanto queda, segun lo que
       este elegido en cada momento. */
    class Resumen : public brls::View
    {
        std::shared_ptr<Estado> estado;

    public:
        explicit Resumen(std::shared_ptr<Estado> e) : estado(std::move(e)) { this->setHeight(118); }

        void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override
        {
            const uint64_t borra = deep_clean::estricta::aBorrar(estado->plan, estado->op);
            const uint64_t queda = estado->plan.usado > borra ? estado->plan.usado - borra : 0;
            const float gap = 16.0f, w = (width - gap) / 2.0f, h = height - 24.0f, top = y + 10.0f;
            auto caja = [&](float cx, NVGcolor fondo, NVGcolor texto, const std::string& titulo, const std::string& valor) {
                nvgBeginPath(vg);
                nvgRoundedRect(vg, cx, top, w, h, 12.0f);
                nvgFillColor(vg, this->a(fondo));
                nvgFill(vg);
                nvgFontFaceId(vg, ctx->fontStash->regular);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                nvgFillColor(vg, this->a(texto));
                nvgFontSize(vg, 18.0f);
                nvgBeginPath(vg);
                nvgText(vg, cx + 20.0f, top + 14.0f, titulo.c_str(), nullptr);
                nvgFontSize(vg, 34.0f);
                nvgBeginPath(vg);
                nvgText(vg, cx + 20.0f, top + 38.0f, valor.c_str(), nullptr);
            };
            caja(x, nvgRGBA(226, 75, 74, 46), oqb::rojoTexto(), "menus/limpia/se_borra"_i18n, tam(borra));
            caja(x + w + gap, nvgRGBA(52, 213, 131, 40), oqb::verdeTexto(), "menus/limpia/se_conserva"_i18n,
                 estado->plan.usado ? tam(queda) : "—");
        }

        void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override {}
    };

    /* Paso 1: que pack. Los packs propios del catalogo (los que tienen fecha
       publicada); el que esta en la SD va marcado. */
    class EligePack : public brls::List
    {
    public:
        EligePack(brls::StagedAppletFrame* frame, std::shared_ptr<Estado> estado, const nlohmann::ordered_json& nxlinks) : brls::List()
        {
            this->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/limpia/intro"_i18n, true));
            const auto links = download::getLinksFromJson(util::getValueFromKey(util::getValueFromKey(nxlinks, "cfws"), "Atmosphere"));
            const nlohmann::ordered_json publicadas = packVersion::fetchPublished();
            const std::string actual = packVersion::current();
            int packs = 0;
            for (const auto& link : links) {
                const std::string url = link.second;
                if (!packVersion::couldBeTracked(url)) continue;
                const std::string clave = packVersion::keyFromUrl(url);
                const std::string fecha = packVersion::published(publicadas, clave).date;
                if (fecha.empty()) continue;
                brls::ListItem* item = new brls::ListItem(link.first);
                if (clave == actual) item->setValue("menus/limpia/en_tu_sd"_i18n, true);
                const std::string nombre = link.first;
                item->getClickEvent()->subscribe([frame, estado, url, clave, fecha, nombre](brls::View* view) {
                    if (!estado->erista && !std::filesystem::exists(MARIKO_PAYLOAD_PATH)) {
                        util::showDialogBoxInfo("menus/errors/mariko_payload_missing"_i18n);
                        return;
                    }
                    estado->url = url;
                    estado->clave = clave;
                    estado->fecha = fecha;
                    estado->nombre = nombre;
                    frame->nextStage();
                });
                this->addView(item);
                ++packs;
            }
            if (!packs) this->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/limpia/sin_packs"_i18n, true));
            this->registerAction("menus/common/back"_i18n, brls::Key::B, [] {
                brls::Application::popView();
                return true;
            });
        }
    };

    /* Paso 4: vista previa. Nada se borro todavia. */
    class VistaPrevia : public brls::List
    {
    public:
        VistaPrevia(brls::StagedAppletFrame* frame, std::shared_ptr<Estado> estado) : brls::List()
        {
            const auto& plan = estado->plan;
            this->addView(new Resumen(estado));

            this->addView(new brls::Header("menus/limpia/siempre_conserva"_i18n));
            brls::ListItem* juegos = new brls::ListItem("menus/limpia/juegos"_i18n, "", "menus/limpia/juegos_sub"_i18n);
            juegos->setValue("menus/limpia/protegido"_i18n, true);
            this->addView(juegos);
            brls::ListItem* respaldos = new brls::ListItem("menus/limpia/respaldos"_i18n, "", "menus/limpia/respaldos_sub"_i18n);
            respaldos->setValue("menus/limpia/protegido"_i18n, true);
            this->addView(respaldos);

            struct Opcion {
                Categoria cat;
                const char* titulo;
            };
            const Opcion opciones[] = {{deep_clean::estricta::PORTS, "menus/limpia/ports"},
                                       {deep_clean::estricta::APPS, "menus/limpia/apps"},
                                       {deep_clean::estricta::MODS, "menus/limpia/mods"},
                                       {deep_clean::estricta::TEMAS, "menus/limpia/temas"},
                                       {deep_clean::estricta::RAIZ, "menus/limpia/raiz"}};
            bool cabecera = false;
            for (const auto& o : opciones) {
                if (!plan.cuenta[o.cat]) continue;
                if (!cabecera) {
                    this->addView(new brls::Header("menus/limpia/tu_decides"_i18n));
                    cabecera = true;
                }
                brls::ToggleListItem* item = new brls::ToggleListItem(i18n::getStr(o.titulo), estado->op.conservar[o.cat], "",
                                                                      "menus/limpia/se_queda"_i18n, "menus/limpia/se_va"_i18n);
                item->setSubLabel(subtitulo(plan, o.cat));
                const Categoria cat = o.cat;
                item->getClickEvent()->subscribe([estado, item, cat](brls::View* view) { estado->op.conservar[cat] = item->getToggleState(); });
                this->addView(item);
            }

            if (plan.cuenta[deep_clean::estricta::RESTOS]) {
                this->addView(new brls::Header("menus/limpia/siempre_borra"_i18n));
                brls::ListItem* restos = new brls::ListItem("menus/limpia/restos"_i18n, "", subtitulo(plan, deep_clean::estricta::RESTOS));
                this->addView(restos);
            }

            this->addView(new brls::Header(""));
            brls::ListItem* lista = new brls::ListItem("menus/limpia/ver_lista"_i18n);
            lista->getClickEvent()->subscribe([estado](brls::View* view) {
                static const char* CAT[] = {"menus/limpia/cat_resto", "menus/limpia/cat_app", "menus/limpia/cat_mods", "menus/limpia/cat_tema",
                                            "menus/limpia/cat_raiz", "menus/limpia/cat_port"};
                brls::AppletFrame* marco = new brls::AppletFrame(true, true);
                marco->setTitle("menus/limpia/ver_lista"_i18n);
                brls::List* l = new brls::List();
                int n = 0;
                for (const auto& e : estado->plan.elementos) {
                    if (++n > 400) {
                        l->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, fmt::format("menus/limpia/y_mas"_i18n, estado->plan.elementos.size() - 400), true));
                        break;
                    }
                    const bool queda = e.cat != deep_clean::estricta::RESTOS && estado->op.conservar[e.cat];
                    brls::ListItem* it = new brls::ListItem(e.ruta, "", i18n::getStr(CAT[e.cat]) + " · " + tam(e.bytes));
                    it->setValue(queda ? "menus/limpia/se_queda"_i18n : "menus/limpia/se_va"_i18n, queda);
                    l->addView(it);
                }
                if (estado->plan.elementos.empty())
                    l->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/limpia/nada"_i18n, true));
                marco->setContentView(l);
                brls::Application::pushView(marco);
            });
            this->addView(lista);

            PackListItem* seguir = new PackListItem("menus/limpia/continuar"_i18n, "", fmt::format("menus/limpia/continuar_sub"_i18n, estado->nombre));
            seguir->setWarningValue("menus/limpia/continuar_valor"_i18n);
            seguir->getClickEvent()->subscribe([frame](brls::View* view) { frame->nextStage(); });
            this->addView(seguir);

            this->registerAction("menus/limpia/cancelar"_i18n, brls::Key::B, [] {
                volverAlInicio();
                return true;
            });
        }
    };

    /* Paso 5: confirmar manteniendo A tres segundos, para que no salga de un
       toque sin querer. Soltar antes vuelve a cero. */
    class Mantener : public brls::View
    {
        brls::StagedAppletFrame* frame;
        std::shared_ptr<Estado> estado;
        brls::Label* label;
        brls::Button* boton;
        std::chrono::steady_clock::time_point desde{};
        bool apretando = false, listo = false;
        static constexpr float SEGUNDOS = 3.0f;

    public:
        Mantener(brls::StagedAppletFrame* frame, std::shared_ptr<Estado> estado) : frame(frame), estado(std::move(estado))
        {
            this->label = new brls::Label(brls::LabelStyle::DIALOG, "", true);
            this->label->setHorizontalAlign(NVG_ALIGN_CENTER);
            this->label->setParent(this);
            this->boton = (new brls::Button(brls::ButtonStyle::REGULAR))->setLabel("menus/limpia/mantener"_i18n);
            this->boton->setParent(this);
            this->registerAction("menus/limpia/cancelar"_i18n, brls::Key::B, [] {
                volverAlInicio();
                return true;
            });
        }

        ~Mantener()
        {
            delete this->label;
            delete this->boton;
        }

        void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override
        {
            const std::string texto = fmt::format("menus/limpia/confirmar"_i18n, tam(deep_clean::estricta::aBorrar(estado->plan, estado->op)), estado->nombre);
            if (this->label->getText() != texto) {
                this->label->setText(texto);
                this->invalidate();
            }
            this->label->frame(ctx);
            this->boton->frame(ctx);

            GLFWgamepadstate pad{};
            const bool a = glfwGetGamepadState(GLFW_JOYSTICK_1, &pad) && pad.buttons[GLFW_GAMEPAD_BUTTON_A] == GLFW_PRESS;
            float p = 0.0f;
            if (a && !this->listo) {
                if (!this->apretando) {
                    this->apretando = true;
                    this->desde = std::chrono::steady_clock::now();
                }
                p = std::min(1.0f, std::chrono::duration<float>(std::chrono::steady_clock::now() - this->desde).count() / SEGUNDOS);
            }
            else
                this->apretando = false;

            const float bw = 420.0f, bh = 10.0f, bx = x + (width - bw) / 2.0f, by = this->boton->getY() - 34.0f;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, bx, by, bw, bh, bh / 2.0f);
            nvgFillColor(vg, this->a(ctx->theme->sidebarSeparatorColor));
            nvgFill(vg);
            if (p > 0.0f) {
                nvgBeginPath(vg);
                nvgRoundedRect(vg, bx, by, bw * p, bh, bh / 2.0f);
                nvgFillColor(vg, this->a(oqb::doradoTexto()));
                nvgFill(vg);
            }
            if (p >= 1.0f && !this->listo) {
                this->listo = true;
                this->frame->nextStage();
            }
        }

        void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override
        {
            this->label->setWidth(this->width * 0.8f);
            this->label->invalidate(true);
            this->label->setBoundaries(this->x + this->width / 2 - this->label->getWidth() / 2, this->y + (this->height - this->label->getHeight() - this->y - style->CrashFrame.buttonHeight) / 2,
                                       this->label->getWidth(), this->label->getHeight());
            this->boton->setBoundaries(this->x + this->width / 2 - style->CrashFrame.buttonWidth / 2, this->y + (this->height - style->CrashFrame.buttonHeight * 3),
                                       style->CrashFrame.buttonWidth, style->CrashFrame.buttonHeight);
            this->boton->invalidate();
        }

        brls::View* getDefaultFocus() override { return this->boton; }
    };

    /* Paso 4 del primer marco: con el plan listo, abre el segundo (vista previa,
       confirmar, limpiar e instalar, reiniciar). Se arma aca y no al principio
       porque la vista previa necesita el plan para construirse. */
    class Puente : public brls::View
    {
        std::shared_ptr<Estado> estado;
        bool hecho = false;

    public:
        explicit Puente(std::shared_ptr<Estado> e) : estado(std::move(e)) {}

        void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override
        {
            if (this->hecho) return;
            this->hecho = true;
            auto estado = this->estado;
            brls::StagedAppletFrame* marco = new brls::StagedAppletFrame();
            marco->setTitle("menus/limpia/titulo"_i18n);
            marco->addStage(new VistaPrevia(marco, estado));
            marco->addStage(new Mantener(marco, estado));
            marco->addStage((new WorkerPage(marco, "menus/limpia/trabajando"_i18n, [estado]() {
                deep_clean::estricta::ejecutar(estado->plan, estado->op);
                util::extractArchive(contentType::ams_cfw);
                if (ProgressEvent::instance().getStatusCode() <= 399) {
                    packVersion::recordInstalled(estado->clave, estado->fecha);
                    std::error_code ec;
                    std::filesystem::remove(AMS_FILENAME, ec);  // 100+ MB que ya no sirven
                }
            }))->volverNiveles(2));
            marco->addStage(new ConfirmPage_AmsUpdate(marco, "menus/ams_update/reboot_rcm"_i18n, estado->erista));
            brls::Application::pushView(marco);
        }

        void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override {}
    };

}  // namespace

namespace {
    // La explicacion antes de empezar: con "Continuar" en vez del "Atras" de ConfirmPage.
    class Explicacion : public ConfirmPage
    {
    public:
        Explicacion(brls::StagedAppletFrame* frame, const std::string& texto) : ConfirmPage(frame, texto)
        {
            this->button->setLabel("menus/common/continue"_i18n);
            // Es una explicacion de varios pasos: letra de lectura y alineada a la izquierda.
            this->label->setFontSize(brls::Application::getStyle()->Label.regularFontSize);
            this->label->setHorizontalAlign(NVG_ALIGN_LEFT);
        }
    };
}  // namespace

void instalacionLimpia::abrirCon(const nlohmann::ordered_json& nxlinks, bool erista, const std::string& nombre, const std::string& url)
{
    auto estado = std::make_shared<Estado>();
    estado->erista = erista;
    estado->nombre = nombre;
    estado->url = url;
    estado->clave = packVersion::keyFromUrl(url);
    estado->fecha = packVersion::published(packVersion::fetchPublished(), estado->clave).date;
    if (!erista && !std::filesystem::exists(MARIKO_PAYLOAD_PATH)) {
        util::showDialogBoxInfo("menus/errors/mariko_payload_missing"_i18n);
        return;
    }
    brls::StagedAppletFrame* frame = new brls::StagedAppletFrame();
    frame->setTitle("menus/limpia/titulo"_i18n);
    frame->addStage(new Explicacion(frame, fmt::format("menus/limpia/explica"_i18n, nombre)));
    frame->addStage(new WorkerPage(frame, "menus/common/downloading"_i18n, [estado]() {
        deep_clean::logPaso("=== inicio instalacion limpia");
        util::downloadArchive(estado->url, contentType::ams_cfw);
    }));
    frame->addStage(new WorkerPage(frame, "menus/limpia/revisando"_i18n, [estado]() {
        estado->plan = deep_clean::estricta::armar(AMS_FILENAME);
        if (!estado->plan.valido)
            ProgressEvent::instance().setStatusCode(STATUS_INCOMPLETE_DOWNLOAD);
        ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
    }));
    frame->addStage(new Puente(estado));
    brls::Application::pushView(frame);
}

void instalacionLimpia::abrir(const nlohmann::ordered_json& nxlinks, bool erista)
{
    auto estado = std::make_shared<Estado>();
    estado->erista = erista;
    brls::StagedAppletFrame* frame = new brls::StagedAppletFrame();
    frame->setTitle("menus/limpia/titulo"_i18n);
    frame->addStage(new EligePack(frame, estado, nxlinks));
    frame->addStage(new WorkerPage(frame, "menus/common/downloading"_i18n, [estado]() {
        deep_clean::logPaso("=== inicio instalacion limpia");
        util::downloadArchive(estado->url, contentType::ams_cfw);
    }));
    frame->addStage(new WorkerPage(frame, "menus/limpia/revisando"_i18n, [estado]() {
        estado->plan = deep_clean::estricta::armar(AMS_FILENAME);
        if (!estado->plan.valido)
            ProgressEvent::instance().setStatusCode(STATUS_INCOMPLETE_DOWNLOAD);
        ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
    }));
    frame->addStage(new Puente(estado));
    brls::Application::pushView(frame);
}
