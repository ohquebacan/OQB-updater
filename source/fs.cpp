#include "fs.hpp"

#include <borealis.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>

#include "constants.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

namespace fs {
    std::vector<std::string> splitString(const std::string& s, char delimiter)
    {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(s);
        while (std::getline(tokenStream, token, delimiter)) {
            tokens.push_back(token);
        }
        return tokens;
    }

    bool removeDir(const std::string& path)
    {
        Result ret = 0;
        FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc");
        if (R_FAILED(ret = fsFsDeleteDirectoryRecursively(fs, path.c_str())))
            return false;
        return true;
    }

    bool removeDirContentsExcept(const std::string& path, const std::set<std::string>& keep)
    {
        if (keep.empty()) {
            return removeDir(path);
        }

        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            return true;
        }

        // La SD es FAT/exFAT, que no distingue mayúsculas: el title id puede
        // estar escrito de cualquier forma tanto en preserve.txt como en disco.
        const auto startsWithNoCase = [](const std::string& s, const std::string& prefix) {
            if (prefix.size() > s.size()) return false;
            return std::equal(prefix.begin(), prefix.end(), s.begin(), [](char a, char b) {
                return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
            });
        };

        // Se conserva una entrada tanto si está listada como si contiene algo
        // listado: preservar /atmosphere/contents/ABC/flags/boot2.flag tiene
        // que mantener en pie los directorios intermedios.
        const auto preserved = [&keep, &startsWithNoCase](const std::string& entry) {
            for (const auto& k : keep) {
                if (k.empty()) continue;
                if (startsWithNoCase(entry, k) || startsWithNoCase(k, entry)) {
                    return true;
                }
            }
            return false;
        };

        bool ok = true;
        for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
            if (ec) break;

            const std::string entryPath = entry.path().string();
            if (preserved(entryPath)) continue;

            std::error_code removeEc;
            if (entry.is_directory(removeEc)) {
                if (!removeDir(entryPath)) ok = false;
            }
            else {
                std::filesystem::remove(entryPath, removeEc);
                if (removeEc) ok = false;
            }
        }

        return ok;
    }

    nlohmann::ordered_json parseJsonFile(const std::string& path)
    {
        std::ifstream file(path);

        std::string fileContent((std::istreambuf_iterator<char>(file)),
                                (std::istreambuf_iterator<char>()));

        if (nlohmann::ordered_json::accept(fileContent))
            return nlohmann::ordered_json::parse(fileContent);
        else
            return nlohmann::ordered_json::object();
    }

    void writeJsonToFile(nlohmann::ordered_json& data, const std::string& path)
    {
        std::ofstream out(path);
        out << data.dump(4);
    }

    bool copyFile(const std::string& from, const std::string& to)
    {
        std::ifstream src(from, std::ios::binary);
        std::ofstream dst(to, std::ios::binary);

        if (src.good() && dst.good()) {
            dst << src.rdbuf();
            return true;
        }
        return false;
    }

    void createTree(std::string path)
    {
        std::string delimiter = "/";
        size_t pos = 0;
        std::string token;
        std::string directories("");
        while ((pos = path.find(delimiter)) != std::string::npos) {
            token = path.substr(0, pos);
            directories += token + "/";
            std::filesystem::create_directory(directories);
            path.erase(0, pos + delimiter.length());
        }
    }

    std::string copyFiles(const std::string& path)
    {
        std::string error = "";
        if (std::filesystem::exists(path)) {
            std::string str;
            std::ifstream in(path);
            if (in) {
                while (std::getline(in, str)) {
                    if (str.size() > 0) {
                        auto toMove = splitString(str, '|');
                        if (std::filesystem::exists(toMove[0]) && toMove.size() > 1) {
                            copyFile(toMove[0], toMove[1]);
                        }
                        else {
                            error += toMove[0] + "\n";
                        }
                    }
                }
            }
        }
        if (error == "") {
            error = "menus/common/all_done"_i18n;
        }
        else {
            error = "menus/tools/batch_copy_not_found"_i18n + error;
        }
        return error;
    }

    std::set<std::string> readLineByLine(const std::string& path)
    {
        std::set<std::string> res;
        std::ifstream lines(path);
        std::string line;
        if (lines) {
            while (std::getline(lines, line)) {
                if (line.size() > 0) {
                    if (line.back() == '\r')
                        line.pop_back();
                    res.insert(line);
                }
            }
        }
        return res;
    }

    Result getFreeStorageSD(s64& free)
    {
        return nsGetFreeSpaceSize(NcmStorageId_SdCard, &free);
    }

}  // namespace fs