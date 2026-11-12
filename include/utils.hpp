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
    // Cierra `niveles` vistas de la pila, una despues de otra (para volver al
    // punto desde el que se abrio un flujo, sin abrir otra ventana principal).
    void volverAtras(int niveles);
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

    /* Un tema o un menu alternativo activo: romfs, exefs o exefs.nsp en la carpeta
       del menu HOME o de sus applets. */
    bool hayTemaActivo();

    /* Lo que eligio el usuario antes de instalar el pack: conservar el tema (no se
       borran esas carpetas en la limpieza previa) o borrarlo. */
    void setConservarTema(bool conservar);

    /* Quita el tema o menu alternativo activo (romfs, exefs, exefs.nsp de las
       carpetas del menu HOME y sus applets). Lo demas de esas carpetas se queda. */
    void quitarTemaActivo();
    bool installPendingAppUpdate();
    void restartApp();
    /** Cierra la app y abre otro .nro (Daybreak, por ejemplo). Devuelve false,
        sin cerrar nada, si el cargador que abrio la app no sabe hacerlo. */
    bool launchNro(const std::string& path, const std::string& argv);
    /** Abre el .nro de esa ruta de la SD (de appUpdates::rutaParaAbrir). Si esta
        vacia o no se puede saltar, avisa y devuelve false. */
    bool abrirAppInstalada(const std::string& rutaSd, const std::string& nombre, const std::string& argumentos = "");
    bool isErista();
    void removeSysmodulesFlags(const std::string& directory);
    std::string lowerCase(const std::string& str);
    std::string upperCase(const std::string& str);
    std::string getErrorMessage(long status_code);
    // El zip de la base de trucos trae al menos un archivo de trucos. La base
    // 2026-09-08 salio con un contents.zip vacio que la app daba por instalado.
    bool baseTrucosValida(const std::string& zip);
    /* Instalar un pack vacia atmosphere/contents, y con eso los trucos de cada
       juego, pero cheats_version.dat queda: la app daba la base por bajada y cada
       juego "sin trucos". Si no queda ningun truco en la SD se borra la marca,
       para que la base se vuelva a ofrecer. */
    void trazaTrucos(const std::string& paso);
    void revisarBaseTrucos();
    /* Desde el firmware 21, las actualizaciones de los juegos traen los nombres
       comprimidos (zlib, para que entren 32 idiomas en el lugar de 16): el byte
       0x3215 de la NACP vale 1 y libnx los devuelve tal cual, por eso salian
       "?" y simbolos raros. Esto los descomprime en su lugar (los 16 primeros
       idiomas). Ver switchbrew/libnx#714. Devuelve false si no pudo. */
    bool descomprimirNombres(NacpStruct* nacp);
    // Tras instalar el pack: vuelve a poner los trucos desde cheats.zip (no se borra).
    void restaurarTrucos();
    /* Juegos instalados despues de bajar la base: se extraen sus trucos desde
       cheats.zip (sin volver a descargar nada), solo los de los juegos que
       todavia no tienen carpeta de trucos. */
    void completarTrucos();
    /* Los trucos propios de OQB (TRUCOS_OQB_URL) encima de los de la base, para
       los juegos instalados. bajar: pide el archivo (si el guardado tiene mas de
       un dia). forzar: escribe aunque el archivo de la SD no lo hayamos puesto
       nosotros (recien se extrajo la base y lo piso). Sin forzar no se pisa un
       archivo que el usuario haya editado. */
    void aplicarTrucosOqb(bool bajar, bool forzar, const std::vector<std::string>& forzarJuegos = {});
    // La version del archivo de trucos OQB guardado ("" si no hay).
    std::string versionTrucosOqb();
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