#pragma once

#include <json.hpp>
#include <string>
#include <vector>

#include "user_links.hpp"

// Tab "Updates": looks at what is installed on the SD card (homebrew, overlays,
// sysmodules) and offers newer GitHub releases of the ones it knows.
//
// The first half is pure logic (no libnx, no network, unit tested on a PC).
namespace updates {

    struct Entry
    {
        std::string name;
        std::string repo;                 // "owner/repo"
        std::vector<std::string> detect;  // any of these paths existing = installed
        bool extract = false;             // zip that is unpacked ...
        std::string extractTo = "/";      // ... to this folder
        std::string filter;               // optional asset name filter
    std::string installDir;           // where a single downloaded file goes ("" = folder of the detected file)
    bool autoLayout = false;          // zip may be SD layout or app-only: see ziplayout::detect
    };

    enum class Status
    {
        updateAvailable,
        upToDate,
        unknown,  // installed, but neither the file nor our records tell which version
    };

    struct Item
    {
        Entry entry;
        std::string foundPath;  // the detect path that exists
        std::string installed;  // version found on the SD ("" = unknown)
        std::string latest;     // newest release tag ("" = not checked / failed)
        Status status = Status::unknown;
    };

    // ---------------------------------------------------------------- pure logic
    // "v1.2.3-beta" -> "1.2.3", "Version 658" -> "658", "" if no digits.
    std::string normalizeVersion(const std::string& raw);
    // -1 / 0 / 1, or 2 when one side has no number at all.
    int compareVersions(const std::string& a, const std::string& b);
    std::string parentDir(const std::string& path);  // "/switch/a/b.nro" -> "/switch/a/"

    std::vector<Entry> builtinCatalog();
    std::vector<Entry> entriesFromJson(const nlohmann::ordered_json& json);
    // user file entries replace built-in ones with the same name
    std::vector<Entry> mergeCatalogs(std::vector<Entry> base, const std::vector<Entry>& extra);

    // display version stored in the NACP of an .nro / .ovl ("" if none)
    std::string nroVersion(const std::string& path);

    userlinks::Link toLink(const Item& item);
    Status computeStatus(const std::string& installed, const std::string& latest);

    // ------------------------------------------------------------ SD + network
    std::vector<Entry> loadCatalog();
    // Scans the SD card; queries GitHub for entries whose cached answer is older than
    // 6 hours (max. 25 requests per call). Returns the installed entries.
    std::vector<Item> scan(bool allowNetwork);

}  // namespace updates
