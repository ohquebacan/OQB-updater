#pragma once

#include <switch.h>

#include <json.hpp>
#include <set>

namespace fs {
    std::vector<std::string> splitString(const std::string& s, char delimiter);
    bool removeDir(const std::string& path);
    // Vacía `path` salvo las entradas listadas en `keep` (rutas completas desde
    // la raíz de la SD, como las de preserve.txt). El directorio en sí queda.
    bool removeDirContentsExcept(const std::string& path, const std::set<std::string>& keep);
    nlohmann::ordered_json parseJsonFile(const std::string& path);
    void writeJsonToFile(nlohmann::ordered_json& data, const std::string& path);
    bool copyFile(const std::string& from, const std::string& to);
    std::string copyFiles(const std::string& path);
    void createTree(std::string path);
    std::set<std::string> readLineByLine(const std::string& path);
    Result getFreeStorageSD(s64& free);
}  // namespace fs
