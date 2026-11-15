#pragma once

#include <json.hpp>
#include <string>
#include <vector>

// User-defined download links ("Eigene Links" tab).
//
// The first half of this header is pure logic (no libnx, no network) and is unit
// tested on a PC. The second half reads/writes /config/aio-switch-updater/user_links.json
// and talks to the GitHub API.
namespace userlinks {

    enum class Kind
    {
        app = 0,  // .nro homebrew
        theme,    // .nxtheme / theme zip
        cheats,   // cheat zips
        mod,      // game / system mods
        other,    // anything else
        cfw,      // full CFW pack -> shown in "Atmosphere aktualisieren"
    };
    constexpr int KIND_COUNT = 6;

    struct Link
    {
        std::string name;
        std::string url;
        Kind kind = Kind::app;
        std::string saveTo = "/switch/";  // directory (ends with '/'), used when NOT extracting
        bool extract = false;
        std::string extractTo = "/";  // directory (ends with '/'), used when extracting
        std::string filter;           // optional: only assets whose name contains this text
        std::string installedTag;     // release tag that was installed last
        bool autoLayout = false;      // zip: SD-card layout -> extract to "/", else into extractTo (not saved)
    };

    struct Defaults
    {
        std::string saveTo;
        bool extract;
        std::string extractTo;
    };

    struct GithubRef
    {
        std::string owner;
        std::string repo;
        std::string tag;           // from .../releases/tag/<tag>, else empty = latest
        bool directAsset = false;  // .../releases/download/..., .../archive/..., .../raw/...
    };

    struct Asset
    {
        std::string name;
        std::string url;
    };

    struct Resolved
    {
        bool ok = false;
        std::string tag;       // release tag (also set when no suitable asset was found)
        std::string fileName;  // name of the file to download
        std::string url;       // download url
        std::string error;     // "network", "rate_limit", "no_release", "no_assets", "no_match"
    };

    // ---------------------------------------------------------------- pure logic
    std::string kindKey(Kind kind);
    Kind kindFromKey(const std::string& key);
    Defaults defaultsFor(Kind kind);

    // "/switch", "sdmc:/switch/", "switch\\x" -> "/switch/", "/switch/x/". "" if invalid (".." etc.).
    std::string normalizeDir(const std::string& path);

    bool parseGithubUrl(const std::string& url, GithubRef& out);
    bool isGithubRepoLink(const Link& link);  // GitHub repo, i.e. releases can be queried
    std::string fileNameFromUrl(const std::string& url);
    std::string defaultNameFor(const std::string& url);

    std::vector<Asset> assetsFromRelease(const nlohmann::ordered_json& release);
    bool chooseAsset(const std::vector<Asset>& assets, const Link& link, Asset& out);
    Resolved resolveFromRelease(const nlohmann::ordered_json& release, const Link& link);

    nlohmann::ordered_json linksToJson(const std::vector<Link>& links);
    std::vector<Link> linksFromJson(const nlohmann::ordered_json& json);

    // name -> url (key ends with " [PACK]") for the "Atmosphere aktualisieren" list.
    nlohmann::ordered_json packsFromReleases(const nlohmann::ordered_json& releases, const Link& link);

    // ------------------------------------------------- persistence + GitHub API
    std::vector<Link> load();
    bool save(const std::vector<Link>& links);
    bool markInstalled(const Link& link, const std::string& tag);

    Resolved resolve(const Link& link);
    nlohmann::ordered_json cfwPacks();  // cached for the whole run

    // Asks GitHub once per app start for the newest tag of every added repo.
    // Returns how many of them are newer than what was installed.
    int checkForUpdates(const std::vector<Link>& links);
    bool updateAvailable(const Link& link);
    std::string latestKnownTag(const Link& link);

    // newest release tag of owner/repo (pre-releases included as fallback); error codes as in Resolved
    bool fetchLatestTag(const std::string& owner, const std::string& repo, std::string& tag, std::string& error);
    // tag recorded for installs that are not in the user's link list (Updates tab)
    std::string recordedTag(const std::string& url);

}  // namespace userlinks
