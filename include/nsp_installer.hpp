#pragma once

#include <switch.h>

#include <string>
#include <vector>

namespace nspInstaller {
    bool isNspUrl(const std::string& url);
    // Rutas que tendran los NSP despues de extraer el ZIP en la raiz de la SD.
    std::vector<std::string> findInArchive(const std::string& archivePath, const std::string& stripPrefix = "");
    // Instala en la SD y, solo si todo termina bien, borra el NSP temporal.
    Result installAndDelete(const std::string& path);
}
