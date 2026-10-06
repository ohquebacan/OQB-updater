#include "extract.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <minizip/unzip.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "current_cfw.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "main_frame.hpp"
#include "progress_event.hpp"
#include "utils.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

constexpr size_t WRITE_BUFFER_SIZE = 0x10000;

namespace {
    /* En FAT32 da igual la caja, asi que las comparaciones de nombre tambien. */
    std::string enMinusculas(std::string v)
    {
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
        return v;
    }

    std::string nombreDeArchivo(const std::string& ruta)
    {
        const size_t barra = ruta.rfind('/');
        return barra == std::string::npos ? ruta : ruta.substr(barra + 1);
    }

    /* Indice de los .nro que ya estan en /switch, de nombre a ruta. Sirve para
       no dejar una segunda copia de una app que el usuario ya tiene en otro
       sitio: el pack reparte unos sueltos y otros en carpeta propia, y la app,
       al descargar homebrew, usa siempre carpeta propia. Sin esto, quien baje
       una app por la app y luego instale el pack termina con dos entradas en el
       menu homebrew.

       Se recorre con dirent y no con std::filesystem, que con rutas de la SD ya
       nos ha fallado en silencio. Tres niveles alcanzan: /switch/x.nro,
       /switch/app/x.nro y /switch/app/sub/x.nro. */
    void indexarNros(const std::string& carpeta, int profundidad, std::map<std::string, std::string>& indice)
    {
        if (profundidad <= 0) return;

        DIR* dir = opendir(carpeta.c_str());
        if (!dir) return;

        while (struct dirent* entrada = readdir(dir)) {
            const std::string nombre = entrada->d_name;
            if (nombre == "." || nombre == "..") continue;

            const std::string hijo = carpeta + (carpeta.back() == '/' ? "" : "/") + nombre;

            struct stat st;
            if (stat(hijo.c_str(), &st) != 0) continue;

            if (S_ISDIR(st.st_mode)) {
                indexarNros(hijo, profundidad - 1, indice);
            }
            else if (nombre.size() > 4 && enMinusculas(nombre.substr(nombre.size() - 4)) == ".nro") {
                // El primero que aparezca manda; da igual cual, lo que importa
                // es no crear uno nuevo al lado.
                indice.emplace(enMinusculas(nombre), hijo);
            }
        }
        closedir(dir);
    }
}  // namespace

namespace extract {

    namespace {
        bool caselessCompare(const std::string& a, const std::string& b)
        {
            return strcasecmp(a.c_str(), b.c_str()) == 0;
        }

        s64 getUncompressedSize(const std::string& archivePath)
        {
            s64 size = 0;
            unzFile zfile = unzOpen(archivePath.c_str());
            unz_global_info gi;
            unzGetGlobalInfo(zfile, &gi);
            for (uLong i = 0; i < gi.number_entry; ++i) {
                unz_file_info fi;
                unzOpenCurrentFile(zfile);
                unzGetCurrentFileInfo(zfile, &fi, NULL, 0, NULL, 0, NULL, 0);
                size += fi.uncompressed_size;
                unzCloseCurrentFile(zfile);
                unzGoToNextFile(zfile);
            }
            unzClose(zfile);
            return size;  // in B
        }

        void ensureAvailableStorage(const std::string& archivePath)
        {
            s64 uncompressedSize = getUncompressedSize(archivePath);
            s64 freeStorage;

            if (R_SUCCEEDED(fs::getFreeStorageSD(freeStorage))) {
                brls::Logger::info("Uncompressed size of archive {}: {}. Available: {}", archivePath, uncompressedSize, freeStorage);
                if (uncompressedSize * 1.1 > freeStorage) {
                    brls::Application::crash("menus/errors/insufficient_storage"_i18n);
                    std::this_thread::sleep_for(std::chrono::microseconds(2000000));
                    brls::Application::quit();
                }
            }
        }

        void extractEntry(std::string filename, unzFile& zfile, bool forceCreateTree = false)
        {
            if (filename.back() == '/') {
                fs::createTree(filename);
                return;
            }
            // Always create parent directories — some zips omit directory entries
            fs::createTree(filename);
            void* buf = malloc(WRITE_BUFFER_SIZE);
            FILE* outfile = fopen(filename.c_str(), "wb");
            if (!outfile) {
                free(buf);
                return;
            }
            for (int j = unzReadCurrentFile(zfile, buf, WRITE_BUFFER_SIZE); j > 0; j = unzReadCurrentFile(zfile, buf, WRITE_BUFFER_SIZE)) {
                fwrite(buf, 1, j, outfile);
            }
            free(buf);
            fclose(outfile);
        }
    }  // namespace

    std::string detectWrapperDir(const std::string& archivePath)
    {
        // Carpetas que de verdad viven en la raiz de la SD: si el zip trae solo
        // una de estas arriba, NO es un envoltorio y hay que dejarla.
        static const std::set<std::string> sdRoot = {
            "atmosphere", "bootloader", "switch", "config", "Nintendo", "emuMMC",
            "sept", "warmboot_mariko", "payloads", "themes", "games", "ReiNX", "sxos",
        };

        unzFile zfile = unzOpen(archivePath.c_str());
        if (!zfile) return "";

        unz_global_info gi;
        if (unzGetGlobalInfo(zfile, &gi) != UNZ_OK) {
            unzClose(zfile);
            return "";
        }

        std::string candidate;
        bool ok = (gi.number_entry > 0);

        for (uLong i = 0; ok && i < gi.number_entry; ++i) {
            char szFilename[0x301] = "";
            unzGetCurrentFileInfo(zfile, NULL, szFilename, sizeof(szFilename), NULL, 0, NULL, 0);

            const std::string entry = szFilename;
            const auto slash = entry.find('/');
            if (slash == std::string::npos || slash == 0) {
                // Hay algo suelto en la raiz del zip: no es un envoltorio.
                ok = false;
                break;
            }

            const std::string top = entry.substr(0, slash);
            if (candidate.empty()) {
                candidate = top;
            }
            else if (candidate != top) {
                ok = false;
                break;
            }

            unzGoToNextFile(zfile);
        }

        unzClose(zfile);

        if (!ok || candidate.empty() || sdRoot.count(candidate)) {
            return "";
        }
        return candidate + "/";
    }

    void extract(const std::string& archivePath, const std::string& workingPath, bool preserveInis, std::function<void()> func, const std::string& stripPrefix)
    {
        ensureAvailableStorage(archivePath);

        unzFile zfile = unzOpen(archivePath.c_str());
        unz_global_info gi;
        unzGetGlobalInfo(zfile, &gi);

        ProgressEvent::instance().setTotalSteps(gi.number_entry);
        ProgressEvent::instance().setStep(0);

        std::set<std::string> ignoreList = fs::readLineByLine(FILES_IGNORE);
        std::string appPath = util::getAppPath();

        /* Para no duplicar homebrew: si una app que trae el zip ya existe en
           otro sitio de /switch, se escribe encima de la que el usuario tiene en
           vez de dejar una segunda copia. Asi se respeta donde la tenga, con sus
           datos al lado, que es lo que hace la app al descargar homebrew.

           Antes hay que saber que rutas trae el propio zip: si el zip ya incluye
           la ruta donde esta la copia del usuario, no se redirige nada, o un zip
           con dos .nro del mismo nombre acabaria escribiendo los dos encima del
           mismo archivo. */
        std::map<std::string, std::string> nrosEnLaSD;
        std::set<std::string> rutasDelZip;
        {
            unzFile previo = unzOpen(archivePath.c_str());
            if (previo != NULL) {
                unz_global_info giPrevio;
                unzGetGlobalInfo(previo, &giPrevio);
                for (uLong j = 0; j < giPrevio.number_entry; ++j) {
                    char nombre[0x301] = "";
                    unzGetCurrentFileInfo(previo, NULL, nombre, sizeof(nombre), NULL, 0, NULL, 0);
                    std::string ruta = nombre;
                    if (!stripPrefix.empty() && ruta.rfind(stripPrefix, 0) == 0)
                        ruta = ruta.substr(stripPrefix.length());
                    if (!ruta.empty())
                        rutasDelZip.insert(workingPath + ruta);
                    unzGoToNextFile(previo);
                }
                unzClose(previo);
            }

            const bool tocaSwitch = std::any_of(rutasDelZip.begin(), rutasDelZip.end(), [](const std::string& r) {
                return r.rfind(APP_PATH, 0) == 0 && r.size() > 4 && enMinusculas(r.substr(r.size() - 4)) == ".nro";
            });
            if (tocaSwitch)
                indexarNros(APP_PATH, 3, nrosEnLaSD);
        }

        for (uLong i = 0; i < gi.number_entry; ++i) {
            char szFilename[0x301] = "";
            unzOpenCurrentFile(zfile);
            unzGetCurrentFileInfo(zfile, NULL, szFilename, sizeof(szFilename), NULL, 0, NULL, 0);
            std::string entryName = szFilename;
            if (!stripPrefix.empty() && entryName.rfind(stripPrefix, 0) == 0) {
                entryName = entryName.substr(stripPrefix.length());
            }
            if (entryName.empty()) {
                // Era la propia carpeta envoltorio.
                unzCloseCurrentFile(zfile);
                unzGoToNextFile(zfile);
                ProgressEvent::instance().incrementStep(1);
                continue;
            }
            std::string filename = workingPath + entryName;

            // Redirigir a donde el usuario ya tenga esa app, si la tiene.
            if (!nrosEnLaSD.empty() && filename.rfind(APP_PATH, 0) == 0 && filename.size() > 4 && enMinusculas(filename.substr(filename.size() - 4)) == ".nro") {
                const auto yaInstalada = nrosEnLaSD.find(enMinusculas(nombreDeArchivo(filename)));
                if (yaInstalada != nrosEnLaSD.end() && yaInstalada->second != filename && rutasDelZip.count(yaInstalada->second) == 0) {
                    brls::Logger::info("{} ya existe en {}, se escribe ahi", filename, yaInstalada->second);
                    filename = yaInstalada->second;
                }
            }

            if (ProgressEvent::instance().getInterupt()) {
                unzCloseCurrentFile(zfile);
                break;
            }
            // De estos dos archivos dependen las protecciones del pack (prodinfo en
            // blanco, dns_mitm, telemetría). Conservar una copia vieja deja la consola
            // desprotegida sin que el usuario se entere, así que se sobrescriben siempre.
            const bool alwaysOverwrite = std::any_of(std::begin(ALWAYS_OVERWRITE_INIS), std::end(ALWAYS_OVERWRITE_INIS),
                                                     [&filename](const char* critical) { return filename == critical; });

            if (appPath != filename) {
                if (!alwaysOverwrite && ((preserveInis == true && filename.substr(filename.length() - 4) == ".ini") || std::find_if(ignoreList.begin(), ignoreList.end(), [&filename](std::string ignored) {
                                                                                                                    u8 res = filename.find(ignored);
                                                                                                                    return (res == 0 || res == 1); }) != ignoreList.end())) {
                    if (!std::filesystem::exists(filename)) {
                        extractEntry(filename, zfile);
                    }
                }
                else {
                    if ((filename == "/atmosphere/package3") || (filename == "/atmosphere/stratosphere.romfs")) {
                        extractEntry(filename + ".aio", zfile);
                    }
                    else {
                        extractEntry(filename, zfile);
                        if (filename.substr(0, 14) == "/hekate_ctcaer") {
                            fs::copyFile(filename, UPDATE_BIN_PATH);

                            /* Antes se preguntaba si copiarlo tambien a
                               reboot_payload.bin. Ese archivo es el que
                               Atmosphere usa al reiniciar: sin el, un reinicio
                               devuelve al arranque original en vez de a hekate,
                               que no es lo que espera quien acaba de instalar un
                               pack que arranca por hekate. Responder que no
                               dejaba la consola a medias sin decir por que, asi
                               que se copia siempre. */
                            if (CurrentCfw::running_cfw == CFW::ams) {
                                fs::copyFile(UPDATE_BIN_PATH, REBOOT_PAYLOAD_PATH);
                            }
                        }
                    }
                }
            }
            ProgressEvent::instance().setStep(i);
            unzCloseCurrentFile(zfile);
            unzGoToNextFile(zfile);
        }
        unzClose(zfile);
        ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
    }

    std::string findNroInArchive(const std::string& archivePath, const std::string& workingPath, const std::string& stripPrefix)
    {
        unzFile zfile = unzOpen(archivePath.c_str());
        if (!zfile) return "";

        unz_global_info gi;
        if (unzGetGlobalInfo(zfile, &gi) != UNZ_OK) {
            unzClose(zfile);
            return "";
        }

        std::string firstMatch;
        std::string switchMatch;

        for (uLong i = 0; i < gi.number_entry; ++i) {
            char szFilename[0x301] = "";
            unzGetCurrentFileInfo(zfile, NULL, szFilename, sizeof(szFilename), NULL, 0, NULL, 0);

            std::string entry = szFilename;
            if (!stripPrefix.empty() && entry.rfind(stripPrefix, 0) == 0) {
                entry = entry.substr(stripPrefix.length());
            }
            if (entry.length() > 4 && entry.substr(entry.length() - 4) == ".nro") {
                const std::string full = workingPath + entry;
                if (firstMatch.empty()) {
                    firstMatch = full;
                }
                // Un pack puede traer varios nro; el que va a /switch/ es el
                // que el usuario realmente lanza.
                if (switchMatch.empty() && full.rfind("/switch/", 0) == 0) {
                    switchMatch = full;
                    break;
                }
            }

            unzGoToNextFile(zfile);
        }

        unzClose(zfile);
        return switchMatch.empty() ? firstMatch : switchMatch;
    }

    std::vector<std::string> getInstalledTitlesNs()
    {
        std::vector<std::string> titles;

        NsApplicationRecord* records = new NsApplicationRecord[MaxTitleCount]();
        NsApplicationControlData* controlData = NULL;

        s32 recordCount = 0;
        u64 controlSize = 0;

        if (R_SUCCEEDED(nsListApplicationRecord(records, MaxTitleCount, 0, &recordCount))) {
            for (s32 i = 0; i < recordCount; i++) {
                controlSize = 0;
                free(controlData);
                controlData = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));
                if (controlData == NULL) {
                    break;
                }
                else {
                    memset(controlData, 0, sizeof(NsApplicationControlData));
                }

                if (R_FAILED(nsGetApplicationControlData(NsApplicationControlSource_Storage, records[i].application_id, controlData, sizeof(NsApplicationControlData), &controlSize))) continue;

                if (controlSize < sizeof(controlData->nacp)) {
                    continue;
                }

                titles.push_back(util::formatApplicationId(records[i].application_id));
            }
            free(controlData);
        }
        delete[] records;
        std::sort(titles.begin(), titles.end());
        return titles;
    }

    std::vector<std::string> excludeTitles(const std::string& path, const std::vector<std::string>& listedTitles)
    {
        std::vector<std::string> titles;
        std::ifstream file(path);
        std::string name;

        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                std::transform(line.begin(), line.end(), line.begin(), ::toupper);
                for (size_t i = 0; i < listedTitles.size(); i++) {
                    if (line == listedTitles[i]) {
                        titles.push_back(line);
                        break;
                    }
                }
            }
        }

        std::sort(titles.begin(), titles.end());

        std::vector<std::string> diff;
        std::set_difference(listedTitles.begin(), listedTitles.end(), titles.begin(), titles.end(),
                            std::inserter(diff, diff.begin()));
        return diff;
    }

    int computeOffset(CFW cfw)
    {
        switch (cfw) {
            case CFW::ams:
                std::filesystem::create_directory(AMS_PATH);
                std::filesystem::create_directory(AMS_CONTENTS);
                chdir(AMS_PATH);
                return std::string(CONTENTS_PATH).length();
                break;
            case CFW::rnx:
                std::filesystem::create_directory(REINX_PATH);
                std::filesystem::create_directory(REINX_CONTENTS);
                chdir(REINX_PATH);
                return std::string(CONTENTS_PATH).length();
                break;
            case CFW::sxos:
                std::filesystem::create_directory(SXOS_PATH);
                std::filesystem::create_directory(SXOS_TITLES);
                chdir(SXOS_PATH);
                return std::string(TITLES_PATH).length();
                break;
        }
        return 0;
    }

    void extractCheats(const std::string& archivePath, const std::vector<std::string>& titles, CFW cfw, const std::string& version, bool extractAll)
    {
        ensureAvailableStorage(archivePath);

        unzFile zfile = unzOpen(archivePath.c_str());
        unz_global_info gi;
        unzGetGlobalInfo(zfile, &gi);

        ProgressEvent::instance().setTotalSteps(gi.number_entry);
        ProgressEvent::instance().setStep(0);

        int offset = computeOffset(cfw);

        for (uLong i = 0; i < gi.number_entry; ++i) {
            char szFilename[0x301] = "";
            unzOpenCurrentFile(zfile);
            unzGetCurrentFileInfo(zfile, NULL, szFilename, sizeof(szFilename), NULL, 0, NULL, 0);
            std::string filename = szFilename;

            if (ProgressEvent::instance().getInterupt()) {
                unzCloseCurrentFile(zfile);
                break;
            }

            if ((int)filename.size() > offset + 16 + 7 && caselessCompare(filename.substr(offset + 16, 7), "/cheats")) {
                if (extractAll) {
                    extractEntry(filename, zfile);
                }
                else {
                    if (std::find_if(titles.begin(), titles.end(), [&filename, offset](std::string title) {
                            return caselessCompare((title.substr(0, 13)), filename.substr(offset, 13));
                        }) != titles.end()) {
                        extractEntry(filename, zfile);
                    }
                }
            }

            ProgressEvent::instance().setStep(i);
            unzCloseCurrentFile(zfile);
            unzGoToNextFile(zfile);
        }
        unzClose(zfile);
        if (version != "offline" && version != "") {
            util::saveToFile(version, CHEATS_VERSION);
        }
        ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
    }

    void extractAllCheats(const std::string& archivePath, CFW cfw, const std::string& version)
    {
        extractCheats(archivePath, {}, cfw, version, true);
    }

    bool isBID(const std::string& bid)
    {
        for (char const& c : bid) {
            if (!isxdigit(c)) return false;
        }
        return true;
    }

    void writeTitlesToFile(const std::set<std::string>& titles, const std::string& path)
    {
        std::ofstream updatedTitlesFile;
        std::set<std::string>::iterator it = titles.begin();
        updatedTitlesFile.open(path, std::ofstream::out | std::ofstream::trunc);
        if (updatedTitlesFile.is_open()) {
            while (it != titles.end()) {
                updatedTitlesFile << (*it) << std::endl;
                it++;
            }
            updatedTitlesFile.close();
        }
    }

    void removeCheats()
    {
        std::string path = util::getContentsPath();
        ProgressEvent::instance().setTotalSteps(std::distance(std::filesystem::directory_iterator(path), std::filesystem::directory_iterator()) + 1);
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            if (ProgressEvent::instance().getInterupt()) {
                break;
            }
            removeCheatsDirectory(entry.path().string());
            ProgressEvent::instance().incrementStep(1);
        }
        std::filesystem::remove(CHEATS_VERSION);
        ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
    }

    void removeOrphanedCheats()
    {
        auto path = util::getContentsPath();
        std::vector<std::string> titles = getInstalledTitlesNs();
        ProgressEvent::instance().setTotalSteps(std::distance(std::filesystem::directory_iterator(path), std::filesystem::directory_iterator()) + 1);
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            if (ProgressEvent::instance().getInterupt()) {
                break;
            }
            if (std::find_if(titles.begin(), titles.end(), [&entry](std::string title) {
                    return caselessCompare(entry.path().filename(), title);
                }) == titles.end()) {
                removeCheatsDirectory(entry.path().string());
            }
            ProgressEvent::instance().incrementStep(1);
        }
        std::filesystem::remove(CHEATS_VERSION);
        ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
    }

    bool removeCheatsDirectory(const std::string& entry)
    {
        bool res = true;
        std::string cheatsPath = fmt::format("{}/cheats", entry);
        if (std::filesystem::exists(cheatsPath)) res &= fs::removeDir(cheatsPath);
        if (std::filesystem::is_empty(entry)) res &= fs::removeDir(entry);
        return res;
    }

}  // namespace extract
