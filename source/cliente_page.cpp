#include "cliente_page.hpp"

#include <switch.h>

#include <algorithm>
#include <ctime>
#include <vector>

#include "constants.hpp"
#include "crash_report.hpp"
#include "diagnostics_page.hpp"
#include "pack_version.hpp"
#include "protection.hpp"
#include "qrcodegen.h"
#include "utils.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {

    // El QR dibujado con nanovg: un cuadrado por modulo, con su margen blanco.
    class QrView : public brls::View
    {
    private:
        std::vector<uint8_t> qr;
        int lado = 0;

    public:
        explicit QrView(const std::string& texto) : qr(qrcodegen_BUFFER_LEN_MAX)
        {
            std::vector<uint8_t> temp(qrcodegen_BUFFER_LEN_MAX);
            if (qrcodegen_encodeText(texto.c_str(), temp.data(), qr.data(), qrcodegen_Ecc_LOW,
                                     qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
                lado = qrcodegen_getSize(qr.data());
            this->setHeight(420);
        }

        void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override
        {
            if (lado == 0) return;
            const int margen = 4;  // modulos de margen que piden los lectores
            const float celda = std::floor(std::min<float>(height, width) / (lado + 2 * margen));
            const float total = celda * (lado + 2 * margen);
            const float ox = x + (width - total) / 2.0f;
            const float oy = y + (height - total) / 2.0f;

            nvgBeginPath(vg);
            nvgFillColor(vg, nvgRGB(255, 255, 255));
            nvgRect(vg, ox, oy, total, total);
            nvgFill(vg);

            nvgBeginPath(vg);
            nvgFillColor(vg, nvgRGB(0, 0, 0));
            for (int fy = 0; fy < lado; ++fy)
                for (int fx = 0; fx < lado; ++fx)
                    if (qrcodegen_getModule(qr.data(), fx, fy))
                        nvgRect(vg, ox + (fx + margen) * celda, oy + (fy + margen) * celda, celda, celda);
            nvgFill(vg);
        }
    };

    std::string fechaLegible(const std::string& iso)
    {
        if (iso.size() != 10) return iso;
        return iso.substr(8, 2) + "/" + iso.substr(5, 2) + "/" + iso.substr(0, 4);
    }

    std::string version(u64 v)
    {
        return fmt::format("{}.{}.{}", (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    }

    std::string atmosphere()
    {
        u64 v = 0;
        if (R_FAILED(splGetConfig(static_cast<SplConfigItem>(65000), &v))) return "";
        return fmt::format("{}.{}.{}", (v >> 56) & 0xFF, (v >> 48) & 0xFF, (v >> 40) & 0xFF);
    }

    std::string protecciones()
    {
        const auto checks = protection::runOnConsole();
        if (!protection::anyFailed(checks)) return "menus/cliente/prot_ok"_i18n;
        if (protection::failedOnlyByNextendo(checks)) return "menus/cliente/prot_nextendo"_i18n;
        std::string fallan;
        for (const auto& c : checks)
            if (c.status == protection::Status::Fail) fallan += (fallan.empty() ? "" : ", ") + c.name;
        return fmt::format("menus/cliente/prot_fail"_i18n, fallan);
    }

    // Todo lo que OQB necesita para dar soporte sin preguntar. Va en el QR, que
    // abre la pagina de OQB; la firma permite saber si es un cliente de verdad.
    nlohmann::ordered_json datosSoporte(const cliente::Estado& e)
    {
        nlohmann::ordered_json d;
        d["id"] = e.idhex;
        d["n"] = e.nombre;
        d["f"] = e.fecha;
        d["s"] = e.firma;
        d["mod"] = util::isErista() ? "Erista (v1)" : "Mariko (v2)";
        d["fw"] = version(hosversionGet());
        d["ams"] = atmosphere();
        d["app"] = APP_VERSION;

        const std::string pack = packVersion::current();
        if (!pack.empty()) {
            std::string nombre = pack.size() > 4 ? pack.substr(0, pack.size() - 4) : pack;
            d["pack"] = nombre + " · " + fechaLegible(packVersion::installed(pack));
        }

        d["prot"] = protecciones();

        char ahora[20];
        const std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_r(&t, &tm);
        std::strftime(ahora, sizeof(ahora), "%d/%m/%Y %H:%M", &tm);
        d["t"] = ahora;

        // Los tres programas que fallaron mas recientemente.
        nlohmann::ordered_json err = nlohmann::ordered_json::array();
        const auto grupos = crash::group(crash::readAll(CRASH_REPORTS_PATH));
        for (size_t i = 0; i < grupos.size() && i < 3; ++i)
            err.push_back({crashProgramName(grupos[i]), grupos[i].count, grupos[i].lastResult, crash::date(grupos[i].lastTimestamp)});
        d["err"] = err;
        return d;
    }

}  // namespace

ClientePage::ClientePage(std::function<void(const cliente::Estado&)> alActivar)
    : brls::AppletFrame(true, true), alActivar(std::move(alActivar))
{
    this->list = new brls::List();
    const cliente::Estado estado = cliente::cargar();
    if (estado.valido)
        this->construirSoporte(estado);
    else
        this->construirRegistro(estado);
    this->setContentView(this->list);
}

void ClientePage::construirRegistro(const cliente::Estado& estado)
{
    if (estado.idhex.empty()) {
        this->list->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/cliente/no_id"_i18n, true));
        return;
    }

    this->list->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/cliente/registro_desc"_i18n, true));

    brls::ListItem* codigo = new brls::ListItem("menus/cliente/codigo"_i18n);
    codigo->setValue(cliente::codigoDe(estado.idhex));
    this->list->addView(codigo);

    brls::ListItem* activar = new brls::ListItem("menus/cliente/activar"_i18n, "menus/cliente/activar_desc"_i18n);
    activar->getClickEvent()->subscribe([this](brls::View* view) {
        cliente::Estado nuevo;
        switch (cliente::activar(nuevo)) {
            case cliente::Activacion::Ok:
                if (this->alActivar) this->alActivar(nuevo);
                util::showDialogBoxInfo(fmt::format("menus/cliente/activado"_i18n, nuevo.nombre));
                brls::Application::popView();
                break;
            case cliente::Activacion::NoRegistrada:
                util::showDialogBoxInfo("menus/cliente/no_registrada"_i18n);
                break;
            case cliente::Activacion::SinRed:
                util::showDialogBoxInfo("menus/cliente/sin_red"_i18n);
                break;
            case cliente::Activacion::Invalida:
                util::showDialogBoxInfo("menus/cliente/invalida"_i18n);
                break;
        }
    });
    this->list->addView(activar);
}

void ClientePage::construirSoporte(const cliente::Estado& estado)
{
    this->list->addView(new brls::Label(brls::LabelStyle::DESCRIPTION,
                                        fmt::format("menus/cliente/es_cliente"_i18n, estado.nombre, fechaLegible(estado.fecha)), true));

    const nlohmann::ordered_json datos = datosSoporte(estado);
    this->list->addView(new QrView(cliente::urlSoporte(datos)));
    this->list->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/cliente/soporte_desc"_i18n, true));

    // Lo mismo que lleva el QR, legible, por si la foto sale mal.
    const auto fila = [this](const std::string& k, const std::string& v) {
        if (v.empty()) return;
        brls::ListItem* item = new brls::ListItem(k);
        item->setHeight(LISTITEM_HEIGHT);
        item->setValue(v, true);
        this->list->addView(item);
    };
    fila("menus/cliente/codigo"_i18n, cliente::codigoDe(estado.idhex));
    fila("menus/about/status_firmware"_i18n, datos.value("fw", ""));
    fila("menus/cliente/ams"_i18n, datos.value("ams", ""));
    fila("menus/cliente/pack"_i18n, datos.value("pack", ""));
    fila("menus/about/status_protection"_i18n, datos.value("prot", ""));
}
