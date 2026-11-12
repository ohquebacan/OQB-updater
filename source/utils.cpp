#include <cctype>
#include <cstring>
#include "utils.hpp"

#include <switch.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <set>

#include "current_cfw.hpp"
#include "actividad.hpp"
#include "deep_clean.hpp"
#include "download.hpp"
#include "extract.hpp"
#include "fs.hpp"
#include "main_frame.hpp"
#include <minizip/unzip.h>
#include <zlib.h>
#include "progress_event.hpp"
#include "reboot_payload.h"
#include "unistd.h"

extern "C" u32 __nx_applet_exit_mode;

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace {

// Registro de pasos de la instalación del pack, para diagnosticar en la SD
// donde se quedó si algo frena. Apunta al mismo deep_clean.log que usa
// deep_clean: la instalación completa queda en un solo archivo.
void pasoInstalacion(const char* p)
{
    deep_clean::logPaso(p);
}

}  // namespace

namespace util {

    namespace {
        constexpr const char* CARPETAS_TEMA[] = {"0100000000001000", "0100000000001007", "0100000000001013"};
        bool g_conservarTema = false;
    }  // namespace


    bool isArchive(const std::string& path)
    {
        if (std::filesystem::exists(path)) {
            std::ifstream is(path, std::ifstream::binary);
            char zip_signature[4] = {0x50, 0x4B, 0x03, 0x04};  // zip signature header PK\3\4
            char signature[4];
            is.read(signature, 4);
            if (!is.good() || !std::equal(std::begin(signature), std::end(signature), std::begin(zip_signature), std::end(zip_signature))) {
                return false;
            }

            /* La cabecera sola no basta: una descarga cortada a la mitad empieza
               igual que una completa. El indice de un zip va al final del
               archivo, asi que si se puede abrir y tiene entradas, el archivo
               llego entero. Sin esto, un pack a medias pasaba por bueno, se
               vaciaba bootloader/ para extraerlo y no se extraia nada. */
            unzFile zfile = unzOpen(path.c_str());
            if (!zfile) return false;
            unz_global_info gi;
            const bool ok = unzGetGlobalInfo(zfile, &gi) == UNZ_OK && gi.number_entry > 0;
            unzClose(zfile);
            return ok;
        }
        return false;
    }

    bool prepareMarikoPayload()
    {
        constexpr std::uintmax_t MaxPayloadSize = 0x20000;  // 128 KB, el limite del modchip
        const auto valid = [](const char* path) {
            std::error_code ec;
            const auto size = std::filesystem::file_size(path, ec);
            return !ec && size > 0 && size <= MaxPayloadSize;
        };

        const char* next = valid(UPDATE_BIN_PATH) ? UPDATE_BIN_PATH : valid(REBOOT_PAYLOAD_PATH) ? REBOOT_PAYLOAD_PATH : nullptr;
        if (next == nullptr) {
            brls::Logger::error("No hay un payload valido para payload.bin.aio; payload.bin no se toca");
            return false;
        }
        if (!fs::copyFile(next, MARIKO_PAYLOAD_PATH_TEMP) || !valid(MARIKO_PAYLOAD_PATH_TEMP)) {
            std::filesystem::remove(MARIKO_PAYLOAD_PATH_TEMP);
            return false;
        }
        return fs::copyFile(RCM_PAYLOAD_PATH, MARIKO_PAYLOAD_PATH);
    }

    void downloadArchive(const std::string& url, contentType type)
    {
        long status_code;
        downloadArchive(url, type, status_code);
    }

    void downloadArchive(const std::string& url, contentType type, long& status_code)
    {
        fs::createTree(DOWNLOAD_PATH);
        switch (type) {
            case contentType::custom:
                status_code = download::downloadFile(url, CUSTOM_FILENAME, OFF);
                break;
            case contentType::cheats:
                status_code = download::downloadFile(url, CHEATS_FILENAME, OFF);
                break;
            case contentType::fw:
                status_code = download::downloadFile(url, FIRMWARE_FILENAME, OFF);
                break;
            case contentType::app:
                status_code = download::downloadFile(url, APP_FILENAME, OFF);
                break;
            case contentType::bootloaders:
                status_code = download::downloadFile(url, BOOTLOADER_FILENAME, OFF);
                break;
            case contentType::ams_cfw:
                status_code = download::downloadFile(url, AMS_FILENAME, OFF);
                break;
            case contentType::apps:
                status_code = download::downloadFile(url, CUSTOM_FILENAME, OFF);
                break;
            default:
                break;
        }
        /* 0 es que no hubo respuesta o que se corto a la mitad (sin red, modo
           avion, wifi que se cae). Antes no contaba como error y se pasaba a
           extraer lo que hubiera quedado. */
        if (status_code == 0) status_code = STATUS_DOWNLOAD_FAILED;
        ProgressEvent::instance().setStatusCode(status_code);
    }

    void volverAtras(int niveles)
    {
        if (niveles <= 0) return;
        // Cada cierre en el callback del anterior: dos popView seguidos sacarian
        // dos veces la misma vista, porque sigue en la pila mientras se anima.
        brls::Application::popView(brls::ViewAnimation::FADE, [niveles] { volverAtras(niveles - 1); });
    }

    void showDialogBoxInfo(const std::string& text)
    {
        brls::Dialog* dialog;
        dialog = new brls::Dialog(text);
        brls::GenericEvent::Callback callback = [dialog](brls::View* view) {
            dialog->close();
        };
        dialog->addButton("menus/common/ok"_i18n, callback);
        dialog->setCancelable(true);
        dialog->open();
    }

    int showDialogBoxBlocking(const std::string& text, const std::string& opt)
    {
        int result = -1;
        brls::Dialog* dialog = new brls::Dialog(text);
        brls::GenericEvent::Callback callback = [dialog, &result](brls::View* view) {
            result = 0;
            dialog->close();
        };
        dialog->addButton(opt, callback);
        dialog->setCancelable(false);
        dialog->open();
        while (result == -1) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
        std::this_thread::sleep_for(std::chrono::microseconds(800000));
        return result;
    }

    int showDialogBoxBlocking(const std::string& text, const std::string& opt1, const std::string& opt2)
    {
        int result = -1;
        brls::Dialog* dialog = new brls::Dialog(text);
        brls::GenericEvent::Callback callback1 = [dialog, &result](brls::View* view) {
            result = 0;
            dialog->close();
        };
        brls::GenericEvent::Callback callback2 = [dialog, &result](brls::View* view) {
            result = 1;
            dialog->close();
        };
        dialog->addButton(opt1, callback1);
        dialog->addButton(opt2, callback2);
        dialog->setCancelable(false);
        dialog->open();
        while (result == -1) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
        std::this_thread::sleep_for(std::chrono::microseconds(800000));
        return result;
    }

    /* Borra lo que el pack lista en pack_remove.txt y despues la lista. Solo
       rutas absolutas dentro de /switch, /config o /atmosphere y sin "..": la
       lista viene en un zip descargado y no puede tocar nada mas de la SD. Las
       versiones viejas de la app no la conocen y solo la dejan en la SD. */
    void removePackLeftovers()
    {
        if (!std::filesystem::exists(PACK_REMOVE_TXT))
            return;
        actividad::paso(actividad::texto("obsoletos"));

        std::ifstream lista(PACK_REMOVE_TXT);
        std::string linea;
        while (std::getline(lista, linea)) {
            while (!linea.empty() && (linea.back() == '\r' || linea.back() == ' '))
                linea.pop_back();
            if (linea.empty() || linea[0] == '#')
                continue;
            const bool permitida = (linea.rfind("/switch/", 0) == 0 || linea.rfind("/config/", 0) == 0 ||
                                    linea.rfind("/atmosphere/", 0) == 0) &&
                                   linea.find("..") == std::string::npos;
            if (!permitida) {
                brls::Logger::error("pack_remove.txt: se ignora {}", linea);
                continue;
            }
            /* Lo mismo que usa removeDirContentsExcept, que ya esta probado en
               la consola. remove_all no sirve aca: en la Switch no borraba los
               archivos sueltos (las carpetas si) y el error quedaba en ec. */
            std::error_code ec;
            if (std::filesystem::is_directory(linea, ec)) {
                if (!fs::removeDir(linea))
                    brls::Logger::error("pack_remove.txt: no se pudo borrar {}", linea);
            }
            else if (std::filesystem::exists(linea, ec)) {
                std::filesystem::remove(linea, ec);
                if (ec)
                    brls::Logger::error("pack_remove.txt: no se pudo borrar {}: {}", linea, ec.message());
            }
        }
        lista.close();
        std::filesystem::remove(PACK_REMOVE_TXT);
    }

    // false si el archivo descargado no es un zip completo. Quien llama tiene
    // que parar: el error queda anotado para que la pagina lo muestre.
    bool crashIfNotArchive(contentType type)
    {
        std::string filename;
        switch (type) {
            case contentType::custom:
                filename = CUSTOM_FILENAME;
                break;
            case contentType::cheats:
                filename = CHEATS_FILENAME;
                break;
            case contentType::fw:
                filename = FIRMWARE_FILENAME;
                break;
            case contentType::app:
                filename = APP_FILENAME;
                break;
            case contentType::bootloaders:
                filename = BOOTLOADER_FILENAME;
                break;
            case contentType::ams_cfw:
                filename = AMS_FILENAME;
                break;
            case contentType::apps:
                filename = CUSTOM_FILENAME;
                break;
            default:
                return true;
        }
        if (!isArchive(filename)) {
            ProgressEvent::instance().setStatusCode(STATUS_INCOMPLETE_DOWNLOAD);
            ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
            return false;
        }
        return true;
    }

    void extractArchive(contentType type, const std::string& version)
    {
        chdir(ROOT_PATH);
        /* Antes esto solo anotaba el error y seguia: con un zip vacio (sin
           conexion) o cortado, el pack vaciaba atmosphere/contents y bootloader/
           y despues no tenia nada que extraer, y la consola quedaba sin hekate. */
        if (!crashIfNotArchive(type)) return;
        switch (type) {
            case contentType::cheats: {
                /* Una base sin ningun archivo de trucos no se da por instalada: la
                   2026-09-08 salio con un contents.zip vacio y la app anotaba la
                   version igual, asi que todo figuraba "al dia" y sin trucos. */
                if (!util::baseTrucosValida(CHEATS_FILENAME)) {
                    ProgressEvent::instance().setStatusCode(STATUS_EMPTY_CHEATS);
                    break;
                }
                std::vector<std::string> titles = extract::getInstalledTitlesNs();
                titles = extract::excludeTitles(CHEATS_EXCLUDE, titles);
                extract::extractCheats(CHEATS_FILENAME, titles, CurrentCfw::running_cfw, version);
                // Encima, los propios de OQB (pisan los de la base que se acaban de poner).
                if (!ProgressEvent::instance().getInterupt()) aplicarTrucosOqb(true, true);
                break;
            }
            case contentType::fw:
                fs::removeDir(FIRMWARE_PATH);
                fs::createTree(FIRMWARE_PATH);
                extract::extract(FIRMWARE_FILENAME, FIRMWARE_PATH);
                break;
            case contentType::app:
                extract::extract(APP_FILENAME, CONFIG_PATH);
                fs::copyFile(ROMFS_FORWARDER, FORWARDER_PATH);
                break;
            case contentType::custom: {
                int preserveInis = showDialogBoxBlocking("menus/utils/overwrite_inis"_i18n, "menus/common/yes"_i18n, "menus/common/no"_i18n);
                extract::extract(CUSTOM_FILENAME, ROOT_PATH, preserveInis);
                break;
            }
            case contentType::bootloaders: {
                int preserveInis = showDialogBoxBlocking("menus/utils/overwrite_inis"_i18n, "menus/common/yes"_i18n, "menus/common/no"_i18n);
                extract::extract(BOOTLOADER_FILENAME, ROOT_PATH, preserveInis);
                break;
            }
            case contentType::ams_cfw: {
                /* El espacio se mira antes de limpiar: si faltara a mitad de la
                   extraccion, la SD quedaria sin el pack viejo y sin el nuevo. */
                if (!extract::hayEspacioPara(AMS_FILENAME)) {
                    ProgressEvent::instance().setStatusCode(STATUS_NO_SPACE);
                    break;
                }
                /* Antes aqui se preguntaban dos cosas: si sobrescribir los .ini y
                   si desactivar la carga de sysmodules. Las dos quedan fijadas en
                   el valor que deja el pack funcionando tal como se armo, porque
                   la pregunta no ayudaba: quien instala un pack no tiene como
                   saber que responder, y una respuesta equivocada deja la consola
                   sin arrancar o con los sysmodules apagados sin explicacion.

                   Los .ini se sobrescriben: el pack trae su propio hekate_ipl.ini
                   con las entradas de arranque, y conservar el anterior es
                   justamente lo que hace que el pack no arranque como debe.

                   Los sysmodules no se desactivan: lo de arriba ya borra de
                   atmosphere/contents todo lo que no este en preserve.txt, asi
                   que lo que sobrevive es lo que el pack trae o lo que el usuario
                   pidio conservar, y apagarlo seria romperlo a proposito. */
                constexpr bool preserveInis = false;
                pasoInstalacion("=== inicio instalacion pack");
                actividad::paso(actividad::texto("quitando_anterior"));
                // Limpiar antes de extraer el HATS pack para evitar conflictos de arranque.
                // preserve.txt ya se respetaba al extraer, pero no acá: lo que
                // estuviera listado se borraba igual antes de llegar a esa parte.
                {
                    auto preserved = fs::readLineByLine(FILES_IGNORE);
                    for (const auto& folder : AMS_CONTENTS_KEEP) {
                        preserved.insert(std::string(AMS_CONTENTS) + folder);
                    }
                    /* El tema o menu alternativo se queda si el usuario lo pidio
                       (pregunta antes de instalar; solo sale si hay uno activo). */
                    if (g_conservarTema)
                        for (const char* tid : CARPETAS_TEMA) preserved.insert(std::string(AMS_CONTENTS) + tid);
                    g_conservarTema = false;
                    fs::removeDirContentsExcept("/SaltySD", preserved);
                    fs::removeDirContentsExcept(AMS_CONTENTS, preserved);

                    /* bootloader tambien, por lo mismo que contents: lo que el
                       pack ya no trae se quedaba en la SD y seguia surtiendo
                       efecto. El caso que lo destapo fue bootlogo.bmp, que se
                       quito del pack y hekate seguia mostrandolo porque lo toma
                       de su ruta por omision aunque nadie lo declare.

                       Se conservan dos cosas. ini/, porque son entradas de
                       arranque que el usuario agrega y no le corresponde al pack
                       borrarlas. Y payloads/, porque ahi la gente deja los suyos;
                       el pack repone los que trae de todos modos. El resto (res/,
                       sys/, los .ini sueltos y cualquier archivo heredado) se
                       rehace con lo que venga en el zip. */
                    auto preservedBoot = preserved;
                    preservedBoot.insert(std::string(BOOTLOADER_PATH) + "ini");
                    preservedBoot.insert(std::string(BOOTLOADER_PATH) + "payloads");
                    fs::removeDirContentsExcept(BOOTLOADER_PATH, preservedBoot);
                }
                pasoInstalacion("limpieza previa ok");
                /* Limpieza profunda de restos de otros packs (misma logica que el
                   cliente PC): sincroniza atmosphere/ y config/ contra las rutas del
                   zip, barre .nro/.ovl ajenos de /switch protegiendo carpetas con
                   datos propios (Bacan-Shop, sphaira, aio-switch-updater) y quita
                   ReiNX/sxos/sept de la raiz. Corre siempre, sin preguntar, igual
                   que la limpieza de arriba. */
                deep_clean::removeForeignPackLeftovers(AMS_FILENAME);
                pasoInstalacion("deep_clean ok");
                /* Una lista que haya quedado de antes (un pack copiado a mano no
                   la procesa) se aplicaria a este pack y podria borrar lo que
                   acaba de instalar. Solo vale la que traiga este zip. */
                std::filesystem::remove(PACK_REMOVE_TXT);
                extract::extract(AMS_FILENAME, ROOT_PATH, preserveInis);
                pasoInstalacion("extract ok");
                removePackLeftovers();
                deep_clean::quitarInisHuerfanos();
                /* La limpieza de arriba vacio atmosphere/contents y con eso los
                   trucos: se reponen desde la base que ya estaba bajada. */
                restaurarTrucos();
                pasoInstalacion("trucos repuestos");
                pasoInstalacion("=== fin instalacion pack");
                break;
            }
            case contentType::apps: {
                // Hay zips de homebrew envueltos en una carpeta unica (SwitchU usa
                // "sd_out/"). Sin quitarla, el override de atmosphere acaba en
                // /sd_out/atmosphere/... y la app no lo encuentra.
                const std::string wrapper = extract::detectWrapperDir(CUSTOM_FILENAME);
                extract::extract(CUSTOM_FILENAME, ROOT_PATH, false, []() { return; }, wrapper);
                break;
            }
            default:
                break;
        }
        if (type == contentType::ams_cfw || type == contentType::bootloaders || type == contentType::custom)
            fs::copyFiles(COPY_FILES_TXT);
    }

    std::string formatListItemTitle(const std::string& str, size_t maxScore)
    {
        size_t score = 0;
        for (size_t i = 0; i < str.length(); i++) {
            score += std::isupper(str[i]) ? 4 : 3;
            if (score > maxScore) {
                return str.substr(0, i - 1) + "\u2026";
            }
        }
        return str;
    }

    std::string formatApplicationId(u64 ApplicationId)
    {
        return fmt::format("{:016X}", ApplicationId);
    }

    std::vector<std::string> fetchPayloads()
    {
        std::vector<std::string> payloadPaths;
        payloadPaths.push_back(ROOT_PATH);
        if (std::filesystem::exists(PAYLOAD_PATH)) payloadPaths.push_back(PAYLOAD_PATH);
        if (std::filesystem::exists(AMS_PATH)) payloadPaths.push_back(AMS_PATH);
        if (std::filesystem::exists(REINX_PATH)) payloadPaths.push_back(REINX_PATH);
        if (std::filesystem::exists(BOOTLOADER_PATH)) payloadPaths.push_back(BOOTLOADER_PATH);
        if (std::filesystem::exists(BOOTLOADER_PL_PATH)) payloadPaths.push_back(BOOTLOADER_PL_PATH);
        if (std::filesystem::exists(SXOS_PATH)) payloadPaths.push_back(SXOS_PATH);
        if (std::filesystem::exists(ROMFS_PATH)) payloadPaths.push_back(ROMFS_PATH);
        std::vector<std::string> res;
        for (const auto& path : payloadPaths) {
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.path().extension().string() == ".bin") {
                    if (entry.path().string() != FUSEE_SECONDARY && entry.path().string() != FUSEE_MTC)
                        res.push_back(entry.path().string().c_str());
                }
            }
        }
        return res;
    }

    void shutDown(bool reboot)
    {
        spsmInitialize();
        spsmShutdown(reboot);
    }

    void rebootToPayload(const std::string& path)
    {
        reboot_to_payload(path.c_str(), CurrentCfw::running_cfw != CFW::ams);
    }

    bool hayInternet()
    {
        if (R_FAILED(nifmInitialize(NifmServiceType_User)))
            return true;  // si no se puede preguntar, mejor intentar la peticion

        NifmInternetConnectionType tipo;
        u32 fuerza;
        NifmInternetConnectionStatus estado;
        const Result res = nifmGetInternetConnectionStatus(&tipo, &fuerza, &estado);
        nifmExit();

        if (R_FAILED(res))
            return true;

        return estado == NifmInternetConnectionStatus_Connected;
    }

    std::string getLatestTag(const std::string& url)
    {
        nlohmann::ordered_json tag;
        download::getRequest(url, tag, {"accept: application/vnd.github.v3+json"});
        if (tag.find("tag_name") != tag.end())
            return tag["tag_name"];
        else
            return "";
    }

    std::string downloadFileToString(const std::string& url)
    {
        std::vector<uint8_t> bytes;
        download::downloadFile(url, bytes);
        std::string str(bytes.begin(), bytes.end());
        return str;
    }

    std::string getCheatsVersion()
    {
        /* Se consulta una vez por sesion: la arma la precarga del arranque, en
           segundo plano. Antes la pestana Trucos la pedia al armarse, en el
           hilo de la interfaz (0.6 s de cada apertura), y sin conexion se
           quedaba esperando los timeouts. */
        static std::mutex candado;
        static std::string guardada;
        static bool intentada = false;
        std::lock_guard<std::mutex> l(candado);
        /* Una sola vez por sesion, salga bien o mal: si fallo en la precarga,
           volver a pedirla al abrir Trucos congelaria la interfaz otra vez. */
        if (intentada)
            return guardada;
        intentada = true;
        if (!hayInternet())
            return "";
        auto limpia = [](std::string v) {
            while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back())))
                v.pop_back();
            while (!v.empty() && std::isspace(static_cast<unsigned char>(v.front())))
                v.erase(v.begin());
            // Una pagina de error (el 504 de GitHub) no es una version.
            return (v.size() > 40 || v.find('<') != std::string::npos || v.find('\n') != std::string::npos) ? std::string() : v;
        };
        /* Con tope corto: antes esperaba lo que tardara GitHub (11 s con un 504)
           y la ventana no aparecia hasta entonces. Y se guardaba la pagina de
           error como si fuera la version. */
        std::string texto;
        if (download::textoCorto(CHEATS_URL_VERSION, texto, 4) == 200)
            guardada = limpia(texto);
        if (guardada.empty() && download::textoCorto(CHEATS_URL_RELEASE, texto, 4) == 200) {
            const auto j = nlohmann::ordered_json::parse(texto, nullptr, false);
            if (j.is_object() && j.contains("tag_name") && j["tag_name"].is_string())
                guardada = limpia(j["tag_name"].get<std::string>());
        }
        /* if (res == "" && isArchive(CHEATS_FILENAME)) {
            res = "offline";
        } */
        return guardada;
    }

    void saveToFile(const std::string& text, const std::string& path)
    {
        std::ofstream file(path);
        file << text << std::endl;
    }

    std::string readFile(const std::string& path)
    {
        std::string text = "";
        std::ifstream file(path);
        if (file.good()) {
            file >> text;
        }
        return text;
    }

    std::string getAppPath()
    {
        if (envHasArgv()) {
            std::smatch match;
            std::string argv = (char*)envGetArgv();
            if (std::regex_match(argv, match, std::regex(NRO_PATH_REGEX))) {
                if (match.size() >= 2) {
                    return match[1].str();
                }
            }
        }
        return NRO_PATH;
    }

    bool hayTemaActivo()
    {
        std::error_code ec;
        const std::string contents = getContentsPath();
        for (const char* tid : CARPETAS_TEMA)
            for (const char* activo : {"romfs", "exefs", "exefs.nsp"})
                if (std::filesystem::exists(contents + tid + "/" + activo, ec)) return true;
        return false;
    }

    void setConservarTema(bool conservar) { g_conservarTema = conservar; }

    void quitarTemaActivo()
    {
        const std::string contents = getContentsPath();
        std::error_code ec;
        for (const char* tid : CARPETAS_TEMA) {
            const std::string base = contents + tid + "/";
            for (const char* dir : {"romfs", "exefs"})
                if (std::filesystem::is_directory(base + dir, ec)) fs::removeDir(base + dir);
            std::filesystem::remove(base + "exefs.nsp", ec);
        }
    }

    bool isForwarder()
    {
        /* Abierta desde su icono del menu HOME (un forwarder instalado): corre
           como juego, no dentro del Album. */
        return appletGetAppletType() == AppletType_Application;
    }

    bool installPendingAppUpdate()
    {
        /* Pone la version recien extraida en su sitio desde la propia app, sin
           el forwarder de actualizacion. Desde el icono del menu HOME, cerrar la
           app para saltar a otro NRO deja la consola en mal estado: la app se
           cierra por error y despues todo lo que se abre tumba el menu HOME. El
           NRO en uso ya esta cargado en memoria y su romfs se cierra antes. */
        std::error_code ec;
        const std::string destino = getAppPath();
        // Registro de cada paso, para poder ver en la SD donde se corto.
        FILE* log = fopen(APP_UPDATE_LOG_PATH, "w");
        auto anotar = [log](const std::string& linea) {
            if (log) {
                fputs((linea + "\n").c_str(), log);
                fflush(log);
            }
        };
        anotar("argv: " + std::string(envHasArgv() ? (const char*)envGetArgv() : "(sin argv)"));
        anotar("destino: " + destino);
        const bool hay = std::filesystem::exists(APP_UPDATE_NRO_PATH, ec);
        anotar(fmt::format("nuevo existe: {} ({})", hay, ec.message()));
        if (!hay) {
            if (log) fclose(log);
            return false;
        }
        /* El romfs esta montado dos veces (borealis y main), y cada montaje
           tiene abierto el propio .nro. Con uno solo abierto el borrado no hace
           nada, asi que se desmontan todos. */
        int montajes = 0;
        while (R_SUCCEEDED(romfsExit()) && montajes < 8)
            montajes++;
        anotar(fmt::format("romfs desmontado: {}", montajes));
        ec.clear();
        std::filesystem::remove(destino, ec);
        anotar("borrar destino: " + ec.message());
        ec.clear();
        std::filesystem::rename(APP_UPDATE_NRO_PATH, destino, ec);
        anotar("mover: " + ec.message());
        const bool ok = !ec;
        if (ok) {
            std::filesystem::remove(FORWARDER_PATH, ec);
            anotar("listo");
        }
        if (log) fclose(log);
        return ok;
    }

    void restartApp()
    {
        if (isForwarder()) {
            // Sin saltar a otro NRO: se cierra al menu HOME y se vuelve a abrir.
            romfsExit();
            brls::Application::quit();
            return;
        }
        std::string path = "sdmc:" + getAppPath();
        std::string argv = "\"" + path + "\"";
        envSetNextLoad(path.c_str(), argv.c_str());
        romfsExit();
        brls::Application::quit();
    }

    bool launchNro(const std::string& path, const std::string& argv)
    {
        /* Quien abre el .nro siguiente es el cargador que abrio la app (el del
           Album o el del forwarder del menu HOME), y solo si lo admite. */
        if (!envHasNextLoad() || R_FAILED(envSetNextLoad(path.c_str(), argv.c_str())))
            return false;
        /* Desde el icono del menu HOME, main() hace que la app cierre directo al
           menu HOME (desde la 3.1, por la autoactualizacion). Para saltar hay que
           volver al cargador, como hacia antes y como hacen los otros forks. */
        if (isForwarder())
            __nx_applet_exit_mode = 0;
        /* Las romfs (borealis y main) tienen abierto el propio .nro: se cierran
           todas antes de salir, como en installPendingAppUpdate. */
        int montajes = 0;
        while (R_SUCCEEDED(romfsExit()) && montajes < 8)
            montajes++;
        brls::Application::quit();
        return true;
    }

    bool isErista()
    {
        SetSysProductModel model;
        setsysGetProductModel(&model);
        return (model == SetSysProductModel_Nx || model == SetSysProductModel_Copper);
    }

    void removeSysmodulesFlags(const std::string& directory)
    {
        for (const auto& e : std::filesystem::recursive_directory_iterator(directory)) {
            if (e.path().string().find("boot2.flag") != std::string::npos) {
                std::filesystem::remove(e.path());
            }
        }
    }

    std::string lowerCase(const std::string& str)
    {
        std::string res = str;
        std::for_each(res.begin(), res.end(), [](char& c) {
            c = std::tolower(c);
        });
        return res;
    }

    std::string upperCase(const std::string& str)
    {
        std::string res = str;
        std::for_each(res.begin(), res.end(), [](char& c) {
            c = std::toupper(c);
        });
        return res;
    }

    bool descomprimirNombres(NacpStruct* nacp)
    {
        static_assert(sizeof(NacpStruct) == 0x4000, "NACP de 0x4000");
        u8* crudo = reinterpret_cast<u8*>(nacp);
        if (crudo[0x3215] != 1) return true;  // sin comprimir: nada que hacer
        const u16 tam = static_cast<u16>(crudo[0] | (crudo[1] << 8));
        if (tam == 0 || tam > 0x3000 - 2) return false;
        std::vector<u8> salida(0x6000);
        z_stream z{};
        if (inflateInit2(&z, -15) != Z_OK) return false;
        z.next_in = crudo + 2;
        z.avail_in = tam;
        z.next_out = salida.data();
        z.avail_out = salida.size();
        const int r = inflate(&z, Z_FINISH);
        const uLong hecho = z.total_out;
        inflateEnd(&z);
        if (r != Z_STREAM_END || hecho < 0x3000) return false;
        std::memcpy(crudo, salida.data(), 0x3000);
        crudo[0x3215] = 0;
        return true;
    }

    void revisarBaseTrucos()
    {
        std::error_code ec;
        if (!std::filesystem::exists(CHEATS_VERSION, ec)) return;
        const std::string contents = getContentsPath();
        for (const auto& d : std::filesystem::directory_iterator(contents, ec))
            if (std::filesystem::is_directory(d.path().string() + "/cheats", ec)) return;
        std::filesystem::remove(CHEATS_VERSION, ec);
    }

    void restaurarTrucos()
    {
        std::string version = readFile(CHEATS_VERSION);
        while (!version.empty() && std::isspace(static_cast<unsigned char>(version.back()))) version.pop_back();
        if (version.empty()) return;
        std::error_code ec;
        if (!std::filesystem::exists(CHEATS_FILENAME, ec) || !baseTrucosValida(CHEATS_FILENAME)) {
            std::filesystem::remove(CHEATS_VERSION, ec);
            return;
        }
        std::vector<std::string> titles = extract::getInstalledTitlesNs();
        titles = extract::excludeTitles(CHEATS_EXCLUDE, titles);
        extract::extractCheats(CHEATS_FILENAME, titles, CurrentCfw::running_cfw, version);
        aplicarTrucosOqb(false, true);
    }

    // Un archivo de la base (cheats.zip), o "" si no esta.
    std::string archivoDeLaBase(const std::string& ruta)
    {
        std::string out;
        if (unzFile z = unzOpen(CHEATS_FILENAME)) {
            if (unzLocateFile(z, ruta.c_str(), 2) == UNZ_OK && unzOpenCurrentFile(z) == UNZ_OK) {
                char buf[4096];
                int n;
                while ((n = unzReadCurrentFile(z, buf, sizeof(buf))) > 0 && out.size() < (4u << 20)) out.append(buf, n);
                unzCloseCurrentFile(z);
            }
            unzClose(z);
        }
        return out;
    }

    std::string versionTrucosOqb()
    {
        std::ifstream f(TRUCOS_OQB);
        if (!f) return "";
        const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
        return j.is_object() && j.contains("version") && j["version"].is_string() ? j["version"].get<std::string>() : "";
    }

    void aplicarTrucosOqb(bool bajar, bool forzar, const std::vector<std::string>& forzarJuegos)
    {
        std::error_code ec;
        if (bajar) {
            bool viejo = true;
            if (std::filesystem::exists(TRUCOS_OQB, ec)) {
                const auto t = std::filesystem::last_write_time(TRUCOS_OQB, ec);
                viejo = ec || std::filesystem::file_time_type::clock::now() - t > std::chrono::hours(24);
            }
            std::string texto;
            if (viejo && download::textoCorto(TRUCOS_OQB_URL, texto, 20, 4u << 20) == 200) {
                const nlohmann::json j = nlohmann::json::parse(texto, nullptr, false);
                if (j.is_object() && j.contains("juegos") && j["juegos"].is_object()) {
                    const std::string tmp = std::string(TRUCOS_OQB) + ".tmp";
                    {
                        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                        f << texto;
                    }
                    std::filesystem::remove(TRUCOS_OQB, ec);
                    std::filesystem::rename(tmp, TRUCOS_OQB, ec);
                }
            }
        }
        std::ifstream f(TRUCOS_OQB);
        if (!f) return;
        const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
        if (!j.is_object() || !j.contains("juegos") || !j["juegos"].is_object()) return;

        // Lo que pusimos nosotros (ruta -> hash), para no pisar lo que el usuario edite.
        nlohmann::json puestos = nlohmann::json::object();
        {
            std::ifstream p(TRUCOS_OQB_PUESTOS);
            if (p) {
                puestos = nlohmann::json::parse(p, nullptr, false);
                if (!puestos.is_object()) puestos = nlohmann::json::object();
            }
        }
        const auto hashDe = [](const std::string& s) { return std::to_string(std::hash<std::string>{}(s)); };
        const std::string contents = getContentsPath();
        std::set<std::string> forzados;
        for (const auto& t : forzarJuegos) forzados.insert(upperCase(t.substr(0, 16)));
        bool cambio = false;
        for (const auto& t : extract::excludeTitles(CHEATS_EXCLUDE, extract::getInstalledTitlesNs())) {
            const std::string tid = upperCase(t.substr(0, 16));
            if (!j["juegos"].contains(tid) || !j["juegos"][tid].is_object()) continue;
            for (const auto& [bidCrudo, texto] : j["juegos"][tid].items()) {
                const std::string bid = upperCase(bidCrudo);
                if (!texto.is_string() || bid.size() != 16 || !extract::isBID(bid)) continue;
                const std::string nuevo = texto.get<std::string>();
                const std::string ruta = contents + tid + "/cheats/" + bid + ".txt";
                const std::string actual = std::filesystem::exists(ruta, ec) ? readFile(ruta) : std::string();
                if (actual == nuevo) continue;
                const bool nuestro = puestos.contains(ruta) && puestos[ruta].is_string() && puestos[ruta].get<std::string>() == hashDe(actual);
                /* Sin forzar se pisa: lo que pusimos nosotros, o el archivo tal cual
                   vino de la base (asi se arregla tambien a quien ya la tenia). */
                if (!actual.empty() && !forzar && !nuestro && !forzados.count(tid) &&
                    actual != archivoDeLaBase("contents/" + tid + "/cheats/" + bid + ".txt"))
                    continue;
                std::filesystem::create_directories(contents + tid + "/cheats", ec);
                const std::string tmp = ruta + ".tmp";
                {
                    std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
                    o << nuevo;
                }
                std::filesystem::remove(ruta, ec);
                std::filesystem::rename(tmp, ruta, ec);
                puestos[ruta] = hashDe(nuevo);
                cambio = true;
            }
        }
        if (cambio) {
            std::ofstream p(TRUCOS_OQB_PUESTOS, std::ios::trunc);
            p << puestos.dump();
        }
    }

    void completarTrucos()
    {
        std::string version = readFile(CHEATS_VERSION);
        while (!version.empty() && std::isspace(static_cast<unsigned char>(version.back()))) version.pop_back();
        std::error_code ec;
        if (version.empty() || !std::filesystem::exists(CHEATS_FILENAME, ec)) return;
        const std::string contents = getContentsPath();
        std::vector<std::string> faltan;
        for (const auto& t : extract::excludeTitles(CHEATS_EXCLUDE, extract::getInstalledTitlesNs()))
            if (!std::filesystem::is_directory(contents + t + "/cheats", ec)) faltan.push_back(t);
        if (!faltan.empty()) extract::extractCheats(CHEATS_FILENAME, faltan, CurrentCfw::running_cfw, version);
        // Los propios de OQB: los recien extraidos se pisan; lo demas, solo si lo pusimos nosotros.
        aplicarTrucosOqb(true, false, faltan);
    }

    bool baseTrucosValida(const std::string& zip)
    {
        bool hay = false;
        if (unzFile z = unzOpen(zip.c_str())) {
            for (int r = unzGoToFirstFile(z); r == UNZ_OK && !hay; r = unzGoToNextFile(z)) {
                char nombre[512] = {};
                unz_file_info info{};
                if (unzGetCurrentFileInfo(z, &info, nombre, sizeof(nombre) - 1, nullptr, 0, nullptr, 0) == UNZ_OK &&
                    lowerCase(nombre).find("/cheats/") != std::string::npos && info.uncompressed_size > 0)
                    hay = true;
            }
            unzClose(z);
        }
        return hay;
    }

    std::string getErrorMessage(long status_code)
    {
        std::string res;
        switch (status_code) {
            case STATUS_DOWNLOAD_FAILED:
                res = "menus/errors/download_failed"_i18n;
                break;
            case STATUS_INCOMPLETE_DOWNLOAD:
                res = "menus/errors/incomplete_download"_i18n;
                break;
            case STATUS_EMPTY_CHEATS:
                res = "menus/errors/empty_cheats"_i18n;
                break;
            case STATUS_NO_SPACE:
                res = "menus/errors/insufficient_storage"_i18n;
                break;
            case 500:
                res = fmt::format("{0:}: Internal Server Error", status_code);
                break;
            case 503:
                res = fmt::format("{0:}: Service Temporarily Unavailable", status_code);
                break;
            default:
                res = fmt::format("error: {0:}", status_code);
                break;
        }
        return res;
    }

    bool isApplet()
    {
        AppletType at = appletGetAppletType();
        return at != AppletType_Application && at != AppletType_SystemApplication;
    }

    std::set<std::string> getExistingCheatsTids()
    {
        std::string path = getContentsPath();
        std::set<std::string> res;
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            std::string cheatsPath = entry.path().string() + "/cheats";
            if (std::filesystem::exists(cheatsPath)) {
                res.insert(util::upperCase(cheatsPath.substr(cheatsPath.length() - 7 - 16, 16)));
            }
        }
        return res;
    }

    // Muchas apps viven en /switch/<app>/<app>.nro y guardan su configuración y
    // sus datos al lado. Si bajáramos siempre a /switch/<app>.nro quedarían dos
    // copias: la suelta se abre sin sus datos y el acceso directo apunta a ella.
    namespace {
        // El índice de /switch de la LecturaSwitch activa.
        struct IndiceSwitch {
            int activas = 0;
            int generacion = 0;
            std::set<std::string> rutas;          // archivos y carpetas, en minúsculas (FAT no distingue)
            std::vector<std::string> carpetas;    // subcarpetas de /switch, en el orden del disco
        } indice;

        std::string minus(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s;
        }

        // ¿La ruta está dentro de lo que el índice conoce? /switch/x y /switch/x/y.
        bool cubiertaPorIndice(const std::string& baja)
        {
            const std::string base = minus(APP_PATH);
            if (baja.rfind(base, 0) != 0)
                return false;
            const std::string resto = baja.substr(base.size());
            return !resto.empty() && std::count(resto.begin(), resto.end(), '/') <= 1 && resto.back() != '/';
        }
    }  // namespace

    LecturaSwitch::LecturaSwitch()
    {
        if (indice.activas++ > 0)
            return;
        indice.generacion++;
        indice.rutas.clear();
        indice.carpetas.clear();
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(APP_PATH, ec)) {
            if (ec) break;
            const std::string ruta = std::string(APP_PATH) + entry.path().filename().string();
            indice.rutas.insert(minus(ruta));
            if (!entry.is_directory(ec)) continue;
            indice.carpetas.push_back(ruta);
            std::error_code ec2;
            for (const auto& hijo : std::filesystem::directory_iterator(ruta, ec2)) {
                if (ec2) break;
                indice.rutas.insert(minus(ruta + "/" + hijo.path().filename().string()));
            }
        }
    }

    LecturaSwitch::~LecturaSwitch()
    {
        if (--indice.activas == 0) {
            indice.rutas.clear();
            indice.carpetas.clear();
        }
    }

    int generacionLectura()
    {
        return indice.activas > 0 ? indice.generacion : 0;
    }

    bool existeEnSd(const std::string& ruta)
    {
        if (indice.activas > 0) {
            const std::string baja = minus(ruta);
            if (cubiertaPorIndice(baja))
                return indice.rutas.count(baja) > 0;
        }
        std::error_code ec;
        return std::filesystem::exists(ruta, ec);
    }

    std::string resolveNroDestination(const std::string& filename)
    {
        std::error_code ec;
        const std::string stem = filename.substr(0, filename.rfind(".nro"));

        // 1. Su carpeta propia, la convención de hbmenu.
        const std::string inOwnDir = std::string(APP_PATH) + stem + "/" + filename;
        if (existeEnSd(inOwnDir)) {
            return inOwnDir;
        }

        // 2. Ya estaba suelto en /switch/.
        const std::string flat = std::string(APP_PATH) + filename;
        if (existeEnSd(flat)) {
            return flat;
        }

        // 3. En una carpeta con otro nombre (p. ej. /switch/thegoonies/TheGooniesInstaller.nro).
        if (indice.activas > 0) {
            for (const auto& carpeta : indice.carpetas) {
                const std::string candidate = carpeta + "/" + filename;
                if (existeEnSd(candidate))
                    return candidate;
            }
        }
        else if (std::filesystem::exists(APP_PATH, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(APP_PATH, ec)) {
                if (ec) break;
                if (!entry.is_directory(ec)) continue;
                const std::string candidate = entry.path().string() + "/" + filename;
                if (std::filesystem::exists(candidate, ec)) {
                    return candidate;
                }
            }
        }

        // 4. Instalación nueva: su carpeta propia, no suelto en /switch/.
        //
        // Va acá a propósito. Hay apps que se reubican solas en el primer
        // arranque: pipenSX se mueve de /switch/pipensx.nro a
        // /switch/pipensx/pipensx.nro y se reinicia. Si la dejáramos suelta, el
        // acceso directo que se crea al descargarla apuntaría a una ruta que la
        // app abandona acto seguido, y al abrirlo hbl aborta con "ruta no
        // encontrada". Instalándola ya en su carpeta no hay nada que mover y el
        // acceso directo sigue siendo válido. Además es la convención de hbmenu,
        // la misma que prefiere la regla 1.
        return inOwnDir;
    }

    std::vector<std::string> nroCandidatePaths(const std::string& filename)
    {
        std::error_code ec;
        const std::string stem = filename.substr(0, filename.rfind(".nro"));
        std::vector<std::string> out;

        const auto push = [&out](const std::string& p) {
            if (std::find(out.begin(), out.end(), p) == out.end()) {
                out.push_back(p);
            }
        };

        // Mismo orden que resolveNroDestination. Las dos primeras se construyen
        // aunque no existan: la ubicación que la app ya abandonó es justo la que
        // deja el acceso directo apuntando a la nada.
        push(std::string(APP_PATH) + stem + "/" + filename);
        push(std::string(APP_PATH) + filename);

        if (std::filesystem::exists(APP_PATH, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(APP_PATH, ec)) {
                if (ec) break;
                if (!entry.is_directory(ec)) continue;
                push(entry.path().string() + "/" + filename);
            }
        }

        return out;
    }

    std::string getContentsPath()
    {
        std::string path;
        switch (CurrentCfw::running_cfw) {
            case CFW::ams:
                path = std::string(AMS_PATH) + std::string(CONTENTS_PATH);
                break;
            case CFW::rnx:
                path = std::string(REINX_PATH) + std::string(CONTENTS_PATH);
                break;
            case CFW::sxos:
                path = std::string(SXOS_PATH) + std::string(TITLES_PATH);
                break;
        }
        return path;
    }

    bool getBoolValue(const nlohmann::ordered_json& jsonFile, const std::string& key)
    {
        return (jsonFile.find(key) != jsonFile.end()) ? jsonFile.at(key).get<bool>() : false;
    }

    const nlohmann::ordered_json getValueFromKey(const nlohmann::ordered_json& jsonFile, const std::string& key)
    {
        return (jsonFile.find(key) != jsonFile.end()) ? jsonFile.at(key) : nlohmann::ordered_json::object();
    }

    int openWebBrowser(const std::string url)
    {
        Result rc = 0;
        int at = appletGetAppletType();
        if (at == AppletType_Application) {  // Running as a title
            WebCommonConfig conf;
            WebCommonReply out;
            rc = webPageCreate(&conf, url.c_str());
            if (R_FAILED(rc))
                return rc;
            webConfigSetJsExtension(&conf, true);
            webConfigSetPageCache(&conf, true);
            webConfigSetBootLoadingIcon(&conf, true);
            webConfigSetWhitelist(&conf, ".*");
            rc = webConfigShow(&conf, &out);
            if (R_FAILED(rc))
                return rc;
        }
        else {  // Running under applet
            showDialogBoxInfo("menus/utils/applet_webbrowser"_i18n);
        }
        return rc;
    }

}  // namespace util