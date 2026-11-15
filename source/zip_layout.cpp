#include "zip_layout.hpp"

#include <minizip/unzip.h>

#include <cctype>
#include <set>
#include <string>

#include "extract.hpp"

namespace ziplayout {

    namespace {
        std::string lower(std::string s)
        {
            for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }
    }  // namespace

    Layout detect(const std::string& archivePath, const std::string& fallbackDir)
    {
        static const std::set<std::string> sdRoot = {
            "atmosphere", "bootloader", "switch", "config", "nintendo", "emummc",
            "sept", "payloads", "themes", "games", "reinx", "sxos", "saltysd",
        };

        Layout layout;
        layout.dir = fallbackDir;
        layout.strip = extract::detectWrapperDir(archivePath);

        unzFile zfile = unzOpen(archivePath.c_str());
        if (!zfile) return layout;
        unz_global_info gi;
        if (unzGetGlobalInfo(zfile, &gi) != UNZ_OK) {
            unzClose(zfile);
            return layout;
        }
        for (uLong i = 0; i < gi.number_entry; ++i) {
            char name[0x301] = "";
            unzGetCurrentFileInfo(zfile, NULL, name, sizeof(name), NULL, 0, NULL, 0);
            std::string entry = name;
            if (!layout.strip.empty() && entry.rfind(layout.strip, 0) == 0) entry = entry.substr(layout.strip.size());
            const size_t slash = entry.find('/');
            if (slash != std::string::npos && slash > 0 && sdRoot.count(lower(entry.substr(0, slash)))) {
                layout.dir = "/";
                break;
            }
            unzGoToNextFile(zfile);
        }
        unzClose(zfile);
        return layout;
    }

}  // namespace ziplayout
