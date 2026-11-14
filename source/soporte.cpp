#include "soporte.hpp"

#include <curl/curl.h>
#include <switch.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include <borealis.hpp>
#include <json.hpp>

#include "constants.hpp"
#include "fs.hpp"
#include "oqb_theme.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace soporte {

    namespace {

        const std::string DISPOSITIVO = std::string(CONFIG_PATH) + "reporte-id";
        const std::string ULTIMO = std::string(CONFIG_PATH) + "reporte-ultimo";
        const std::string CONTACTO = std::string(CONFIG_PATH) + "reporte-contacto";

        std::string leer(const std::string& ruta)
        {
            std::ifstream f(ruta);
            std::string s;
            std::getline(f, s);
            return s;
        }

        void escribir(const std::string& ruta, const std::string& valor)
        {
            fs::createTree(CONFIG_PATH);
            std::ofstream f(ruta, std::ios::trunc);
            f << valor << "\n";
        }

        /* Un numero al azar guardado en la SD: agrupa los reportes de la misma
           consola y es lo que bloquea el boton «Bloquear». No sale de nada de
           la consola (ni serie ni cuenta). */
        std::string dispositivo()
        {
            std::string id = leer(DISPOSITIVO);
            const bool valido = id.size() == 16 && std::all_of(id.begin(), id.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            });
            if (valido) return id;
            u8 bytes[8] = {};
            csrngGetRandomBytes(bytes, sizeof(bytes));
            id.clear();
            for (u8 b : bytes) id += fmt::format("{:02x}", b);
            escribir(DISPOSITIVO, id);
            return id;
        }

        std::string porcentaje(const std::string& s)
        {
            std::string out;
            for (unsigned char c : s) {
                if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
                else out += fmt::format("%{:02X}", c);
            }
            return out;
        }

        std::string firmware()
        {
            SetSysFirmwareVersion ver;
            return R_SUCCEEDED(setsysGetFirmwareVersion(&ver)) ? std::string(ver.display_version) : "";
        }

        size_t juntar(void* datos, size_t tam, size_t n, void* destino)
        {
            auto* s = static_cast<std::string*>(destino);
            const size_t total = tam * n;
            if (s->size() < 16 * 1024) s->append(static_cast<char*>(datos), total);
            return total;
        }

        int avance(void* p, curl_off_t, curl_off_t, curl_off_t subirTotal, curl_off_t subido)
        {
            auto* f = static_cast<std::function<void(float)>*>(p);
            if (*f && subirTotal > 0) (*f)(static_cast<float>(subido) / static_cast<float>(subirTotal));
            return 0;
        }

    }  // namespace

    Resultado enviar(const std::string& ruta, const std::string& app, const std::string& mensaje,
                     const std::string& contacto, std::function<void(float)> progreso)
    {
        Resultado r;
        std::ifstream f(ruta, std::ios::binary);
        const std::string cuerpo((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (cuerpo.empty()) {
            r.error = "no_file";
            return r;
        }

        std::vector<std::string> cabeceras = {
            "Content-Type: text/plain; charset=utf-8",
            "X-OQB-App: " + porcentaje(app),
            std::string("X-TSNX-Version: ") + APP_VERSION,
            "X-OQB-Device: " + dispositivo(),
            "X-TSNX-Firmware: " + firmware(),
            "X-TSNX-Lang: " + i18n::getCurrentLocale(),
            "X-TSNX-Contact: " + porcentaje(contacto),
            "X-TSNX-Message: " + porcentaje(mensaje.substr(0, 1200)),
            "Expect:",
        };
        curl_slist* lista = nullptr;
        for (const auto& c : cabeceras) lista = curl_slist_append(lista, c.c_str());

        std::string respuesta;
        CURL* curl = curl_easy_init();
        curl_easy_setopt(curl, CURLOPT_URL, URL);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, cuerpo.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(cuerpo.size()));
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, lista);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "OQB-Updater");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, juntar);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &respuesta);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, avance);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progreso);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
        const CURLcode rc = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.http);
        curl_easy_cleanup(curl);
        curl_slist_free_all(lista);

        if (rc != CURLE_OK) {
            r.http = 0;
            return r;
        }
        const auto j = nlohmann::json::parse(respuesta, nullptr, false);
        if (j.is_object()) {
            r.ok = r.http == 200 && j.value("ok", false);
            r.id = j.value("id", std::string());
            r.error = j.value("error", std::string());
        }
        if (r.ok && r.id.empty()) r.ok = false;
        if (r.ok) escribir(ULTIMO, std::to_string(std::time(nullptr)));
        return r;
    }

    long faltaParaEnviar()
    {
        const std::string s = leer(ULTIMO);
        if (s.empty()) return 0;
        long ultimo = 0;
        try {
            ultimo = std::stol(s);
        }
        catch (...) {
            return 0;
        }
        const long pasaron = static_cast<long>(std::time(nullptr)) - ultimo;
        if (pasaron < 0 || pasaron >= ESPERA_SEGUNDOS) return 0;
        return ESPERA_SEGUNDOS - pasaron;
    }

    std::string contactoGuardado() { return leer(CONTACTO); }

    void guardarContacto(const std::string& contacto) { escribir(CONTACTO, contacto); }

    std::string textoError(const Resultado& r)
    {
        if (r.http == 0 && r.error.empty()) return "menus/soporte/err_red"_i18n;
        if (r.error == "rate_limited") return "menus/soporte/err_minuto"_i18n;
        if (r.error == "daily_limit") return "menus/soporte/err_dia"_i18n;
        if (r.error == "blocked") return "menus/soporte/err_bloqueado"_i18n;
        if (r.error == "delivery_failed" || r.http >= 500) return "menus/soporte/err_servicio"_i18n;
        if (r.error == "too_large") return "menus/soporte/err_grande"_i18n;
        if (r.error == "no_file") return "menus/soporte/err_archivo"_i18n;
        return fmt::format("menus/soporte/err_otro"_i18n, r.http);
    }

}  // namespace soporte

namespace {

    using soporte::Resultado;

    // Lo que hace el hilo del envio; la vista lo lee en cada cuadro.
    struct Envio {
        std::atomic<bool> listo{false};
        std::atomic<float> avance{0.0f};
        Resultado resultado;
    };

    std::string recortar(std::string s)
    {
        const auto no = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), no));
        s.erase(std::find_if(s.rbegin(), s.rend(), no).base(), s.end());
        return s;
    }

    size_t letras(const std::string& s)
    {
        size_t n = 0;
        for (unsigned char c : s)
            if ((c & 0xC0) != 0x80) ++n;
        return n;
    }

    class SoporteView : public brls::View
    {
    public:
        SoporteView(std::string app, std::function<std::string()> generar, std::vector<std::string> incluye)
            : app(std::move(app)), generar(std::move(generar)), incluye(std::move(incluye))
        {
            this->contacto = soporte::contactoGuardado();
            this->registerAction("", brls::Key::DUP, [this] { return this->mover(-1); }, true);
            this->registerAction("", brls::Key::DDOWN, [this] { return this->mover(1); }, true);
            this->registerAction("", brls::Key::DLEFT, [this] { return this->lado(-1); }, true);
            this->registerAction("", brls::Key::DRIGHT, [this] { return this->lado(1); }, true);
            this->registerAction("", brls::Key::A, [this] { return this->pulsarA(); }, true);
            this->registerAction("", brls::Key::B, [this] { return this->pulsarB(); }, true);
            this->registerAction("", brls::Key::X, [this] { return this->enviar(); }, true);
            this->registerAction("", brls::Key::Y, [this] { return this->soloGuardar(); }, true);
        }

        bool isTranslucent() override { return true; }
        brls::View* getDefaultFocus() override { return this; }
        void layout(NVGcontext*, brls::Style*, brls::FontStash*) override {}
        void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;

    private:
        enum class Paso { Formulario, Enviando, Enviado, Error, Guardado };
        enum Foco { Mensaje, Contacto, BotonEnviar, BotonGuardar };

        std::string app;
        std::function<std::string()> generar;
        std::vector<std::string> incluye;
        std::string mensaje, contacto, ruta, error;
        Paso paso = Paso::Formulario;
        int foco = Mensaje;
        std::shared_ptr<Envio> envio;
        Resultado resultado;
        int fuente = -1;
        bool fuenteIntentada = false;

        bool puedeEnviar() const { return letras(recortar(this->mensaje)) >= soporte::MINIMO_MENSAJE && soporte::faltaParaEnviar() == 0; }

        bool mover(int d)
        {
            if (this->paso != Paso::Formulario) return true;
            if (d < 0) this->foco = this->foco >= BotonEnviar ? Contacto : Mensaje;
            else this->foco = this->foco == Mensaje ? Contacto : (this->foco == Contacto ? BotonEnviar : this->foco);
            return true;
        }

        bool lado(int d)
        {
            if (this->paso == Paso::Formulario && this->foco >= BotonEnviar) this->foco = d < 0 ? BotonEnviar : BotonGuardar;
            return true;
        }

        bool pulsarA()
        {
            switch (this->paso) {
                case Paso::Formulario:
                    if (this->foco == Mensaje) {
                        brls::Swkbd::openForText([this](std::string t) { this->mensaje = recortar(t); },
                                                 "menus/soporte/campo_mensaje"_i18n, "menus/soporte/ejemplo"_i18n, 500, this->mensaje, 0, "",
                                                 "menus/soporte/campo_mensaje"_i18n);
                        if (letras(recortar(this->mensaje)) >= soporte::MINIMO_MENSAJE) this->foco = Contacto;
                    }
                    else if (this->foco == Contacto) {
                        brls::Swkbd::openForText([this](std::string t) {
                            t = recortar(t);
                            if (!t.empty() && t[0] == '@') t.erase(0, 1);
                            this->contacto = t;
                            soporte::guardarContacto(t);
                        },
                                                 "menus/soporte/campo_contacto"_i18n, "", 32, this->contacto, 0, "", "usuario");
                        this->foco = BotonEnviar;
                    }
                    else if (this->foco == BotonEnviar) this->enviar();
                    else this->soloGuardar();
                    return true;
                case Paso::Error: this->enviarAhora(); return true;
                case Paso::Enviando: return true;
                default: brls::Application::popView(); return true;
            }
        }

        bool pulsarB()
        {
            if (this->paso != Paso::Enviando) brls::Application::popView();
            return true;
        }

        bool asegurarArchivo()
        {
            if (this->ruta.empty()) this->ruta = this->generar();
            if (this->ruta.empty()) {
                this->resultado = Resultado{};
                this->resultado.error = "no_file";
                this->error = soporte::textoError(this->resultado);
                this->paso = Paso::Error;
                return false;
            }
            return true;
        }

        bool soloGuardar()
        {
            if (this->paso != Paso::Formulario) return true;
            if (this->asegurarArchivo()) this->paso = Paso::Guardado;
            return true;
        }

        bool enviar()
        {
            if (this->paso != Paso::Formulario || !this->puedeEnviar()) return true;
            this->enviarAhora();
            return true;
        }

        void enviarAhora()
        {
            // El archivo se arma justo antes de enviar: asi lleva lo ultimo que paso.
            if (!this->asegurarArchivo()) return;
            this->paso = Paso::Enviando;
            this->envio = std::make_shared<Envio>();
            auto e = this->envio;
            const std::string ruta = this->ruta, app = this->app, mensaje = this->mensaje, contacto = this->contacto;
            std::thread([e, ruta, app, mensaje, contacto] {
                e->resultado = soporte::enviar(ruta, app, mensaje, contacto, [e](float f) { e->avance = f; });
                e->listo = true;
            }).detach();
        }
    };

    void SoporteView::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style*, brls::FrameContext* ctx)
    {
        if (this->paso == Paso::Enviando && this->envio && this->envio->listo) {
            this->resultado = this->envio->resultado;
            this->envio.reset();
            if (this->resultado.ok) this->paso = Paso::Enviado;
            else {
                this->error = soporte::textoError(this->resultado);
                this->paso = Paso::Error;
            }
        }
        if (!this->fuenteIntentada) {
            this->fuenteIntentada = true;
            this->fuente = nvgFindFont(vg, "upheaval");
            if (this->fuente < 0) this->fuente = nvgCreateFont(vg, "upheaval", "romfs:/fonts/upheavtt.ttf");
        }
        const int regular = ctx->fontStash->regular;
        const NVGcolor texto = this->a(ctx->theme->textColor);
        const NVGcolor tenue = this->a(ctx->theme->descriptionColor);
        const NVGcolor foco = this->a(ctx->theme->highlightColor1);
        const NVGcolor acento = this->a(oqb::acentoFuerte());
        const NVGcolor aviso = this->a(oqb::doradoTexto());
        const NVGcolor campo = this->a(oqb::velo(oqb::oscuro() ? 10 : 8));

        // Velo sobre la pantalla de atras y la ventana.
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillColor(vg, this->a(ctx->theme->dialogBackdrop));
        nvgFill(vg);

        const bool formulario = this->paso == Paso::Formulario;
        const float mw = formulario ? 800.0f : 640.0f;
        const float mh = formulario ? 600.0f : (this->paso == Paso::Enviado ? 400.0f : 340.0f);
        const float mx = x + (width - mw) / 2, my = y + (height - mh) / 2;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, mx, my, mw, mh, 18.0f);
        nvgFillColor(vg, this->a(ctx->theme->dialogColor));  // el mismo color que todas las ventanas (oqb_theme)
        nvgFill(vg);
        nvgStrokeColor(vg, this->paso == Paso::Error ? aviso : acento);
        nvgStrokeWidth(vg, 2.0f);
        nvgStroke(vg);

        const float px = mx + 40.0f, ancho = mw - 80.0f;
        float py = my + 36.0f;
        auto titulo = [&](const std::string& t) {
            nvgFontFaceId(vg, this->fuente >= 0 ? this->fuente : regular);
            nvgFontSize(vg, 40.0f);
            nvgTextLetterSpacing(vg, 1.0f);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgFillColor(vg, texto);
            nvgBeginPath(vg);
            nvgText(vg, px, py, t.c_str(), nullptr);
            nvgTextLetterSpacing(vg, 0.0f);
            nvgFontFaceId(vg, regular);
            py += 56.0f;
        };
        // Un parrafo partido en lineas; devuelve el alto usado.
        auto parrafo = [&](const std::string& t, float tx, float ty, float tw, float tam, NVGcolor c, int maximo) {
            nvgFontFaceId(vg, regular);
            nvgFontSize(vg, tam);
            nvgFillColor(vg, c);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            const char* p = t.c_str();
            const char* fin = p + t.size();
            NVGtextRow fila[1];
            int i = 0;
            for (; i < maximo && p < fin; ++i) {
                if (nvgTextBreakLines(vg, p, fin, tw, fila, 1) <= 0) break;
                nvgBeginPath(vg);
                nvgText(vg, tx, ty + i * tam * 1.4f, fila[0].start, fila[0].end);
                p = fila[0].next;
            }
            return i * tam * 1.4f;
        };
        auto boton = [&](float bx, float by, float bw, const std::string& glifo, const std::string& t, bool principal, bool activo, bool conFoco) {
            const float bh = 56.0f;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, bx, by, bw, bh, 12.0f);
            nvgFillColor(vg, principal && activo ? acento : campo);
            nvgFill(vg);
            if (conFoco) {
                nvgBeginPath(vg);
                nvgRoundedRect(vg, bx - 4, by - 4, bw + 8, bh + 8, 15.0f);
                nvgStrokeColor(vg, foco);
                nvgStrokeWidth(vg, 3.0f);
                nvgStroke(vg);
            }
            const std::string s = glifo + "  " + t;
            nvgFontFaceId(vg, regular);
            nvgFontSize(vg, 20.0f);
            NVGcolor c = principal && activo ? this->a(nvgRGB(6, 20, 12)) : texto;
            if (!activo) c.a *= 0.45f;
            nvgFillColor(vg, c);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgBeginPath(vg);
            nvgText(vg, bx + bw / 2, by + bh / 2, s.c_str(), nullptr);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        };
        auto pie = [&](const std::string& t) {
            nvgFontFaceId(vg, regular);
            nvgFontSize(vg, 16.0f);
            nvgFillColor(vg, tenue);
            nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
            nvgBeginPath(vg);
            nvgText(vg, mx + mw - 40.0f, my + mh - 34.0f, t.c_str(), nullptr);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        };
        const std::string copia = this->ruta.empty() ? "" : fmt::format("menus/soporte/copia"_i18n, this->ruta);

        switch (this->paso) {
            case Paso::Formulario: {
                titulo("menus/soporte/titulo"_i18n);
                py += parrafo("menus/soporte/intro"_i18n, px, py, ancho, 18.0f, tenue, 2) + 14.0f;
                nvgFontSize(vg, 15.0f);
                nvgFillColor(vg, tenue);
                nvgBeginPath(vg);
                nvgText(vg, px, py, "menus/soporte/se_envia"_i18n.c_str(), nullptr);
                py += 26.0f;
                // Lo que se envia, en dos columnas.
                for (size_t i = 0; i < this->incluye.size(); ++i) {
                    const float cx = px + (i % 2) * (ancho / 2), cy = py + (i / 2) * 28.0f;
                    nvgBeginPath(vg);
                    nvgCircle(vg, cx + 5, cy + 10, 4.0f);
                    nvgFillColor(vg, acento);
                    nvgFill(vg);
                    parrafo(this->incluye[i], cx + 18, cy, ancho / 2 - 24, 17.0f, texto, 1);
                }
                py += ((this->incluye.size() + 1) / 2) * 28.0f + 14.0f;

                auto caja = [&](int f, const std::string& etiqueta, const std::string& valor, const std::string& vacio, float ch) {
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, px, py, ancho, ch, 12.0f);
                    nvgFillColor(vg, campo);
                    nvgFill(vg);
                    if (this->foco == f) {
                        nvgStrokeColor(vg, foco);
                        nvgStrokeWidth(vg, 3.0f);
                        nvgStroke(vg);
                    }
                    nvgFontSize(vg, 14.0f);
                    nvgFillColor(vg, tenue);
                    nvgBeginPath(vg);
                    nvgText(vg, px + 18, py + 12, etiqueta.c_str(), nullptr);
                    nvgFontSize(vg, 16.0f);
                    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
                    nvgBeginPath(vg);
                    nvgText(vg, px + ancho - 18, py + 12, (" " + "menus/soporte/escribir"_i18n).c_str(), nullptr);
                    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                    parrafo(valor.empty() ? vacio : valor, px + 18, py + 36, ancho - 36, 18.0f, valor.empty() ? tenue : texto,
                            ch > 80 ? 2 : 1);
                    py += ch + 12.0f;
                };
                caja(Mensaje, "menus/soporte/campo_mensaje"_i18n, this->mensaje, "menus/soporte/ejemplo"_i18n, 100.0f);
                caja(Contacto, "menus/soporte/campo_contacto"_i18n, this->contacto.empty() ? "" : "@" + this->contacto, "@usuario", 70.0f);

                const long falta = soporte::faltaParaEnviar();
                std::string nota;
                if (falta > 0) nota = fmt::format("menus/soporte/espera"_i18n, (falta + 59) / 60);
                else if (letras(recortar(this->mensaje)) < soporte::MINIMO_MENSAJE) nota = "menus/soporte/falta_mensaje"_i18n;
                if (!nota.empty()) parrafo(nota, px, py, ancho, 16.0f, aviso, 1);
                py += 30.0f;

                const float bw = (ancho - 16.0f) / 2;
                boton(px, py, bw, "", "menus/soporte/enviar"_i18n, true, this->puedeEnviar(), this->foco == BotonEnviar);
                boton(px + bw + 16.0f, py, bw, "", "menus/soporte/solo_guardar"_i18n, false, true, this->foco == BotonGuardar);
                pie(" " + "menus/soporte/cancelar"_i18n);
                break;
            }
            case Paso::Enviando: {
                titulo("menus/soporte/enviando"_i18n);
                py += parrafo("menus/soporte/no_apagues"_i18n, px, py, ancho, 18.0f, tenue, 2) + 20.0f;
                const float f = std::clamp(this->envio ? this->envio->avance.load() : 0.0f, 0.0f, 1.0f);
                nvgBeginPath(vg);
                nvgRoundedRect(vg, px, py, ancho, 14.0f, 7.0f);
                nvgFillColor(vg, campo);
                nvgFill(vg);
                nvgBeginPath(vg);
                nvgRoundedRect(vg, px, py, std::max(14.0f, ancho * (0.08f + 0.92f * f)), 14.0f, 7.0f);
                nvgFillColor(vg, acento);
                nvgFill(vg);
                py += 40.0f;
                parrafo(copia, px, py, ancho, 15.0f, tenue, 2);
                break;
            }
            case Paso::Enviado: {
                titulo("menus/soporte/enviado"_i18n);
                nvgFontFaceId(vg, this->fuente >= 0 ? this->fuente : regular);
                nvgFontSize(vg, 80.0f);
                nvgTextLetterSpacing(vg, 4.0f);
                nvgFillColor(vg, foco);
                nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
                nvgBeginPath(vg);
                nvgText(vg, mx + mw / 2, py, this->resultado.id.c_str(), nullptr);
                nvgTextLetterSpacing(vg, 0.0f);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                py += 100.0f;
                py += parrafo("menus/soporte/dale_codigo"_i18n, px, py, ancho, 18.0f, texto, 2) + 10.0f;
                parrafo(copia, px, py, ancho, 15.0f, tenue, 2);
                boton(mx + mw / 2 - 120.0f, my + mh - 120.0f, 240.0f, "", "menus/soporte/listo"_i18n, true, true, true);
                break;
            }
            case Paso::Error: {
                titulo("menus/soporte/no_se_pudo"_i18n);
                py += parrafo(this->error, px, py, ancho, 18.0f, texto, 4) + 12.0f;
                if (!copia.empty()) parrafo("menus/soporte/no_se_perdio"_i18n + " " + copia, px, py, ancho, 15.0f, tenue, 3);
                const float bw = (ancho - 16.0f) / 2;
                boton(px, my + mh - 96.0f, bw, "", "menus/soporte/reintentar"_i18n, true, true, true);
                boton(px + bw + 16.0f, my + mh - 96.0f, bw, "", "menus/soporte/cerrar"_i18n, false, true, false);
                break;
            }
            case Paso::Guardado: {
                titulo("menus/soporte/guardado"_i18n);
                py += parrafo(copia, px, py, ancho, 18.0f, texto, 3) + 12.0f;
                parrafo("menus/soporte/guardado_texto"_i18n, px, py, ancho, 16.0f, tenue, 3);
                boton(mx + mw / 2 - 120.0f, my + mh - 96.0f, 240.0f, "", "menus/soporte/listo"_i18n, true, true, true);
                break;
            }
        }
    }

}  // namespace

void abrirEnviarSoporte(const std::string& app, std::function<std::string()> generar, std::vector<std::string> incluye)
{
    brls::Application::pushView(new SoporteView(app, std::move(generar), std::move(incluye)));
}
