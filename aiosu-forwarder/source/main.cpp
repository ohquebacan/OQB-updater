#include <filesystem>
#include <string>

#include <switch.h>

// NRO final en /switch/
#define FULL_PATH      "/switch/OQB-updater.nro"
// NRO recién extraído en /config/ (viene del zip de actualización)
#define CONFIG_NRO     "/config/aio-switch-updater/OQB-updater.nro"
// El forwarder se borra a sí mismo tras lanzar el app
#define FORWARDER_PATH "/config/aio-switch-updater/aiosu-forwarder.nro"

int main(int argc, char* argv[])
{
    // Si hay un NRO nuevo en config, moverlo a /switch/ reemplazando el viejo
    if (std::filesystem::exists(CONFIG_NRO)) {
        std::filesystem::remove(FULL_PATH);
        std::filesystem::rename(CONFIG_NRO, FULL_PATH);
    }

    // El forwarder se autoelimnina para no ocupar espacio
    std::filesystem::remove(FORWARDER_PATH);

    // Lanzar el NRO actualizado
    envSetNextLoad(FULL_PATH, FULL_PATH);
    return 0;
}
