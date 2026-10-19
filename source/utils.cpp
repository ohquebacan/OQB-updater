#include "utils.hpp"

#include <switch.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "current_cfw.hpp"
#include "download.hpp"
#include "extract.hpp"
#include "fs.hpp"
#include "main_frame.hpp"
#include <minizip/unzip.h>
#include "progress_event.hpp"
#include "reboot_payload.h"
#include "unistd.h"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace util {

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
                std::vector<std::string> titles = extract::getInstalledTitlesNs();
                titles = extract::excludeTitles(CHEATS_EXCLUDE, titles);
                extract::extractCheats(CHEATS_FILENAME, titles, CurrentCfw::running_cfw, version);
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
                // Limpiar antes de extraer el HATS pack para evitar conflictos de arranque.
                // preserve.txt ya se respetaba al extraer, pero no acá: lo que
                // estuviera listado se borraba igual antes de llegar a esa parte.
                {
                    auto preserved = fs::readLineByLine(FILES_IGNORE);
                    for (const auto& folder : AMS_CONTENTS_KEEP) {
                        preserved.insert(std::string(AMS_CONTENTS) + folder);
                    }
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
                /* Una lista que haya quedado de antes (un pack copiado a mano no
                   la procesa) se aplicaria a este pack y podria borrar lo que
                   acaba de instalar. Solo vale la que traiga este zip. */
                std::filesystem::remove(PACK_REMOVE_TXT);
                extract::extract(AMS_FILENAME, ROOT_PATH, preserveInis);
                removePackLeftovers();
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
        std::string res = util::downloadFileToString(CHEATS_URL_VERSION);
        /* if (res == "" && isArchive(CHEATS_FILENAME)) {
            res = "offline";
        } */
        return res;
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

    void restartApp()
    {
        std::string path = "sdmc:" + getAppPath();
        std::string argv = "\"" + path + "\"";
        envSetNextLoad(path.c_str(), argv.c_str());
        romfsExit();
        brls::Application::quit();
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
    std::string resolveNroDestination(const std::string& filename)
    {
        std::error_code ec;
        const std::string stem = filename.substr(0, filename.rfind(".nro"));

        // 1. Su carpeta propia, la convención de hbmenu.
        const std::string inOwnDir = std::string(APP_PATH) + stem + "/" + filename;
        if (std::filesystem::exists(inOwnDir, ec)) {
            return inOwnDir;
        }

        // 2. Ya estaba suelto en /switch/.
        const std::string flat = std::string(APP_PATH) + filename;
        if (std::filesystem::exists(flat, ec)) {
            return flat;
        }

        // 3. En una carpeta con otro nombre (p. ej. /switch/thegoonies/TheGooniesInstaller.nro).
        if (std::filesystem::exists(APP_PATH, ec)) {
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