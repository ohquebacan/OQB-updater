#pragma once

#include <switch.h>

#include <borealis.hpp>
#include <json.hpp>
#include <regex>
#include <set>
#include <vector>

#include "constants.hpp"

namespace util {

    typedef char NsApplicationName[0x201];
    typedef uint8_t NsApplicationIcon[0x20000];

    typedef struct
    {
        uint64_t tid;
        NsApplicationName name;
        NsApplicationIcon icon;
        brls::ListItem* listItem;
    } app;

    void clearConsole();
    bool isArchive(const std::string& path);

    /* En Mariko el modchip arranca /payload.bin. Para terminar la instalacion
       se pone ahi el payload de la app, que al arrancar aplica los .aio y deja
       en payload.bin lo que haya en payload.bin.aio. Solo se hace si hay un
       hekate valido para poner detras (existe y cabe en los 128 KB que acepta
       el modchip); si no, payload.bin no se toca. Devuelve si se hizo. */
    bool prepareMarikoPayload();
    void downloadArchive(const std::string& url, contentType type);
    void downloadArchive(const std::string& url, contentType type, long& status_code);
    void extractArchive(contentType type, const std::string& version = "");
    std::string formatListItemTitle(const std::string& str, size_t maxScore = 140);
    std::string formatApplicationId(u64 ApplicationId);
    std::vector<std::string> fetchPayloads();
    void shutDown(bool reboot = false);
    void rebootToPayload(const std::string& path);
    void showDialogBoxInfo(const std::string& text);
    int showDialogBoxBlocking(const std::string& text, const std::string& opt);
    int showDialogBoxBlocking(const std::string& text, const std::string& opt1, const std::string& opt2);
    std::string getLatestTag(const std::string& url);

    /* Si la consola tiene internet, segun el propio sistema. Sirve para no
       gastar el arranque esperando timeouts de peticiones que no pueden salir. */
    bool hayInternet();
    std::string downloadFileToString(const std::string& url);
    std::string getCheatsVersion();
    void saveToFile(const std::string& text, const std::string& path);
    std::string readFile(const std::string& path);
    std::string getAppPath();
    bool isForwarder();
    bool installPendingAppUpdate();
    void restartApp();
    bool isErista();
    void removeSysmodulesFlags(const std::string& directory);
    std::string lowerCase(const std::string& str);
    std::string upperCase(const std::string& str);
    std::string getErrorMessage(long status_code);
    // El zip de la base de trucos trae al menos un archivo de trucos. La base
    // 2026-09-08 salio con un contents.zip vacio que la app daba por instalado.
    bool baseTrucosValida(const std::string& zip);
    bool isApplet();
    std::string getContentsPath();
    // Dónde guardar (o actualizar) un .nro: respeta la carpeta propia de la app si ya existe.
    std::string resolveNroDestination(const std::string& filename);

    // Mientras exista uno de estos, lo que se pregunta sobre /switch sale de una
    // sola lectura de la carpeta (y de sus subcarpetas). Para armar ventanas:
    // preguntaban por cada app del catálogo, varias veces, si existía en cada
    // carpeta de /switch, y eran miles de consultas a la SD (unos 7 s al abrir).
    // Solo mientras se arma: lo que se instale después no estaría en el índice.
    class LecturaSwitch
    {
    public:
        LecturaSwitch();
        ~LecturaSwitch();
        LecturaSwitch(const LecturaSwitch&) = delete;
        LecturaSwitch& operator=(const LecturaSwitch&) = delete;
    };
    // Como std::filesystem::exists, pero con el índice si hay una LecturaSwitch.
    bool existeEnSd(const std::string& ruta);
    // Distinto de 0 mientras haya una LecturaSwitch; cambia con cada una. Para
    // que otros guarden lo leído solo durante esa lectura.
    int generacionLectura();
    // Todas las ubicaciones donde ese .nro pudo haber vivido alguna vez, en el
    // mismo orden que mira resolveNroDestination. Se construyen, no se
    // descubren: la ubicación vieja puede ya no existir en disco y es
    // justamente la que deja un acceso directo huérfano.
    std::vector<std::string> nroCandidatePaths(const std::string& filename);
    bool getBoolValue(const nlohmann::ordered_json& jsonFile, const std::string& key);
    const nlohmann::ordered_json getValueFromKey(const nlohmann::ordered_json& jsonFile, const std::string& key);
    int openWebBrowser(const std::string url);
}  // namespace util