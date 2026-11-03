#include "nextendo_tab.hpp"

#include <switch.h>

#include <algorithm>
#include <cctype>
#include <ctime>

#include "app_updates.hpp"
#include "donar_page.hpp"
#include "pack_list_item.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {

    std::string text(const nlohmann::ordered_json& j, const char* key)
    {
        const auto v = j.find(key);
        return v != j.end() && v->is_string() ? v->get<std::string>() : "";
    }

    std::vector<std::string> lista(const nlohmann::ordered_json& j, const char* key)
    {
        std::vector<std::string> out;
        const auto v = j.find(key);
        if (v != j.end() && v->is_array())
            for (const auto& s : *v)
                if (s.is_string())
                    out.push_back(s.get<std::string>());
        return out;
    }

    /* Texto que puede tener el foco: sin una vista con foco, borealis no
       desplaza la lista y un texto largo queda cortado. */
    class TextoAlcanzable : public brls::Label
    {
    public:
        using brls::Label::Label;
        brls::View* getDefaultFocus() override
        {
            return this;
        }
    };

    // Una ventana con un texto y, si hay, el QR de un enlace debajo.
    void abrirDetalle(const std::string& titulo, const std::string& cuerpo, const std::string& qr = "")
    {
        brls::List* l = new brls::List();
        l->addView(new TextoAlcanzable(brls::LabelStyle::REGULAR, cuerpo, true));
        if (!qr.empty()) {
            l->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/nextendo/scan"_i18n, true));
            QrCodeView* codigo = new QrCodeView(qr);
            codigo->setHeight(300);
            codigo->setFocusable(true);
            l->addView(codigo);
        }
        brls::AppletFrame* frame = new brls::AppletFrame(true, true);
        frame->setContentView(l);
        brls::PopupFrame::open(titulo, frame, "", "");
    }

    std::string vinetas(const std::vector<std::string>& pasos)
    {
        std::string out;
        for (const auto& p : pasos)
            out += (out.empty() ? "" : "\n\n") + std::string("•  ") + p;
        return out;
    }

    void abrirError(const nlohmann::ordered_json& e)
    {
        const std::string cuerpo = text(e, "que_es") + "\n\n" + "menus/nextendo/what_to_do"_i18n + "\n\n" + vinetas(lista(e, "que_hacer"));
        abrirDetalle(text(e, "codigo") + " · " + text(e, "titulo"), cuerpo);
    }

    // "2618 0521", "26180521" y "2618-0521" son el mismo codigo.
    std::string normalizar(const std::string& s)
    {
        std::string out;
        for (unsigned char c : s)
            if (std::isalnum(c))
                out += std::tolower(c);
        return out;
    }

}  // namespace

bool NextendoTab::available(const nlohmann::ordered_json& nxlinks)
{
    return nxlinks.is_object() && nxlinks.contains("nextendo") && nxlinks["nextendo"].is_object();
}

NextendoTab::NextendoTab(const nlohmann::ordered_json& nxlinks) : brls::List(), nxlinks(nxlinks)
{
    const nlohmann::ordered_json& datos = this->nxlinks["nextendo"];

    this->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, "menus/nextendo/intro"_i18n, true));
    this->addSearch(datos);
    this->addChecklist(datos);

    const auto etapas = datos.find("etapas");
    if (etapas != datos.end() && etapas->is_array() && !etapas->empty()) {
        this->addView(new brls::Header("menus/nextendo/stages"_i18n));
        for (const auto& e : *etapas) {
            const std::string titulo = text(e, "titulo");
            const std::string cuerpo = vinetas(lista(e, "pasos"));
            brls::ListItem* item = new brls::ListItem(titulo);
            item->getClickEvent()->subscribe([titulo, cuerpo](brls::View* view) { abrirDetalle(titulo, cuerpo); });
            this->addView(item);
        }
    }

    const auto errores = datos.find("errores");
    if (errores != datos.end() && errores->is_array() && !errores->empty()) {
        this->addView(new brls::Header("menus/nextendo/errors"_i18n));
        for (const auto& e : *errores) {
            brls::ListItem* item = new brls::ListItem(text(e, "codigo"), "", text(e, "titulo"));
            const nlohmann::ordered_json copia = e;
            item->getClickEvent()->subscribe([copia](brls::View* view) { abrirError(copia); });
            this->addView(item);
        }
    }

    const auto notas = datos.find("notas");
    if (notas != datos.end() && notas->is_array() && !notas->empty()) {
        this->addView(new brls::Header("menus/nextendo/notes"_i18n));
        for (const auto& n : *notas) {
            const std::string titulo = text(n, "titulo"), cuerpo = text(n, "texto");
            brls::ListItem* item = new brls::ListItem(titulo);
            item->getClickEvent()->subscribe([titulo, cuerpo](brls::View* view) { abrirDetalle(titulo, cuerpo); });
            this->addView(item);
        }
    }

    this->addView(new brls::Header("menus/nextendo/more"_i18n));
    const std::string video = text(datos, "video"), wiki = text(datos, "wiki");
    if (!video.empty()) {
        brls::ListItem* item = new brls::ListItem("menus/nextendo/video"_i18n);
        item->getClickEvent()->subscribe([video](brls::View* view) {
            abrirDetalle("menus/nextendo/video"_i18n, "menus/firmware/tutorial_text"_i18n, video);
        });
        this->addView(item);
    }
    if (!wiki.empty()) {
        brls::ListItem* item = new brls::ListItem("menus/nextendo/wiki"_i18n);
        item->getClickEvent()->subscribe([wiki](brls::View* view) {
            abrirDetalle("menus/nextendo/wiki"_i18n, "menus/nextendo/wiki_text"_i18n, wiki);
        });
        this->addView(item);
    }
}

void NextendoTab::addSearch(const nlohmann::ordered_json& datos)
{
    brls::ListItem* buscar = new brls::ListItem("menus/nextendo/search"_i18n);
    const nlohmann::ordered_json errores = datos.contains("errores") ? datos["errores"] : nlohmann::ordered_json::array();
    buscar->getClickEvent()->subscribe([errores](brls::View* view) {
        std::string query;
        if (!brls::Swkbd::openForText([&query](std::string t) { query = t; }, "menus/nextendo/search"_i18n, "", 32, "", 0,
                                      "menus/nextendo/search_ok"_i18n, "2618-0521"))
            return;
        const std::string q = normalizar(query);
        if (q.empty())
            return;
        // Primero por codigo; si no, por palabras del titulo.
        for (const auto& e : errores)
            if (normalizar(text(e, "codigo")).find(q) != std::string::npos) {
                abrirError(e);
                return;
            }
        for (const auto& e : errores)
            if (normalizar(text(e, "titulo")).find(q) != std::string::npos) {
                abrirError(e);
                return;
            }
        brls::Application::notify(fmt::format("menus/nextendo/not_found"_i18n, query));
    });
    this->addView(buscar);
}

void NextendoTab::addChecklist(const nlohmann::ordered_json& datos)
{
    this->addView(new brls::Header("menus/nextendo/checklist"_i18n));

    // Firmware: lo dice la consola.
    {
        std::string version;
        SetSysFirmwareVersion ver;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&ver)))
            version = ver.display_version;
        const std::vector<std::string> compatibles = lista(datos, "firmwares");
        const bool ok = std::find(compatibles.begin(), compatibles.end(), version) != compatibles.end();
        std::string lista_txt;
        for (const auto& c : compatibles)
            lista_txt += (lista_txt.empty() ? "" : ", ") + c;
        PackListItem* item = new PackListItem(fmt::format("menus/nextendo/check_firmware"_i18n, version.empty() ? "?" : version), "",
                                              fmt::format("menus/nextendo/check_firmware_sub"_i18n, lista_txt));
        if (ok)
            item->setPositiveValue("menus/nextendo/ok"_i18n);
        else
            item->setWarningValue("menus/nextendo/check"_i18n);
        this->addView(item);
    }

    // Prelude instalado y al dia: lo mismo que la pestana de apps.
    {
        const std::string nombre = text(datos, "prelude");
        std::string url;
        if (this->nxlinks.contains("apps") && this->nxlinks["apps"].is_object() && this->nxlinks["apps"].contains(nombre) && this->nxlinks["apps"][nombre].is_string())
            url = this->nxlinks["apps"][nombre].get<std::string>();
        if (!url.empty()) {
            const bool instalada = appUpdates::isInstalled(this->nxlinks, url);
            const bool nueva = instalada && appUpdates::hasUpdate(this->nxlinks, appUpdates::installed(), url);
            PackListItem* item = new PackListItem("menus/nextendo/check_prelude"_i18n, "", "menus/nextendo/check_prelude_sub"_i18n);
            if (instalada && !nueva)
                item->setPositiveValue("menus/nextendo/ok"_i18n);
            else
                item->setWarningValue(instalada ? "menus/apps/new_version"_i18n : "menus/nextendo/missing"_i18n);
            this->addView(item);
        }
    }

    // Fecha y hora: sin poder compararla con nada, solo se muestra y se avisa si
    // es imposible (anterior a la app). Mal puesta, rompe la conexion segura.
    {
        const std::time_t ahora = std::time(nullptr);
        std::tm local{};
        localtime_r(&ahora, &local);
        char fecha[32];
        std::strftime(fecha, sizeof(fecha), "%d/%m/%Y %H:%M", &local);
        PackListItem* item = new PackListItem("menus/nextendo/check_clock"_i18n, "", "menus/nextendo/check_clock_sub"_i18n);
        if (local.tm_year + 1900 < 2026)
            item->setWarningValue(fecha);
        else
            item->setValue(fecha);
        this->addView(item);
    }

    // Lo que no se puede revisar desde la consola: se abre con sus pasos y su QR.
    const auto checklist = datos.find("checklist");
    if (checklist != datos.end() && checklist->is_array()) {
        for (const auto& c : *checklist) {
            const std::string titulo = text(c, "titulo"), cuerpo = text(c, "texto"), qr = text(c, "qr");
            brls::ListItem* item = new brls::ListItem(titulo);
            item->setValue("menus/nextendo/manual"_i18n, true);
            item->getClickEvent()->subscribe([titulo, cuerpo, qr](brls::View* view) { abrirDetalle(titulo, cuerpo, qr); });
            this->addView(item);
        }
    }
}
