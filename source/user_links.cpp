#include <map>
#include <string>
#include <vector>

#include "download.hpp"
#include "fs.hpp"
#include "github_auth.hpp"
#include "user_links.hpp"

namespace userlinks {

    namespace {
        constexpr const char USER_LINKS_PATH[] = "/config/aio-switch-updater/user_links.json";
        constexpr const char USER_LINKS_DIR[] = "/config/aio-switch-updater/";
        constexpr const char INSTALLED_TAGS_PATH[] = "/config/aio-switch-updater/installed_tags.json";
        constexpr int MAX_UPDATE_CHECKS_LOGGED_IN = 40;
        constexpr int MAX_UPDATE_CHECKS_ANONYMOUS = 5;  // GitHub allows only 60 requests/hour without sign-in

        using json = nlohmann::ordered_json;

        std::vector<std::string> githubHeaders()
        {
            return ghauth::apiHeaders();
        }

        // newest tag per link url, filled once per app start
        std::map<std::string, std::string>& latestTags()
        {
            static std::map<std::string, std::string> tags;
            return tags;
        }

        bool hasTag(const json& release)
        {
            auto it = release.is_object() ? release.find("tag_name") : release.end();
            return release.is_object() && it != release.end() && it->is_string() && !it->get<std::string>().empty();
        }

        bool fetchRelease(const GithubRef& ref, json& release, std::string& error)
        {
            const std::string base = "https://api.github.com/repos/" + ref.owner + "/" + ref.repo + "/";
            const std::string url = ref.tag.empty() ? base + "releases/latest" : base + "releases/tags/" + ref.tag;

            release = json::object();
            long code = download::getRequest(url, release, githubHeaders());
            if (code == 401 && ghauth::isLoggedIn()) {
                // token was revoked or expired: continue without it
                ghauth::logout();
                code = download::getRequest(url, release, githubHeaders());
            }
            if (hasTag(release)) return true;

            if (ref.tag.empty() && code != 403 && code != 429 && code != 0) {
                // "latest" ignores pre-releases: a repo that only has those answers 404.
                json list = json::array();
                download::getRequest(base + "releases?per_page=1", list, githubHeaders());
                if (list.is_array() && !list.empty() && hasTag(list[0])) {
                    release = list[0];
                    return true;
                }
            }
            error = (code == 0) ? "network" : (code == 403 || code == 429) ? "rate_limit"
                                                                            : "no_release";
            return false;
        }
    }  // namespace

    std::vector<Link> load()
    {
        return linksFromJson(fs::parseJsonFile(USER_LINKS_PATH));
    }

    bool save(const std::vector<Link>& links)
    {
        fs::createTree(USER_LINKS_DIR);
        json data = linksToJson(links);
        fs::writeJsonToFile(data, USER_LINKS_PATH);
        return !load().empty() || links.empty();
    }

    bool markInstalled(const Link& link, const std::string& tag)
    {
        std::vector<Link> links = load();
        bool found = false;
        for (auto& l : links) {
            if (l.url == link.url && l.name == link.name) {
                l.installedTag = tag;
                found = true;
            }
        }
        if (found) return save(links);

        // not a user-added link (e.g. an entry of the Updates tab): remember the tag separately
        json rec = fs::parseJsonFile(INSTALLED_TAGS_PATH);
        if (!rec.is_object()) rec = json::object();
        rec[link.url] = tag;
        fs::createTree(USER_LINKS_DIR);
        fs::writeJsonToFile(rec, INSTALLED_TAGS_PATH);
        return true;
    }

    std::string recordedTag(const std::string& url)
    {
        json rec = fs::parseJsonFile(INSTALLED_TAGS_PATH);
        if (!rec.is_object()) return "";
        auto it = rec.find(url);
        return (it != rec.end() && it->is_string()) ? it->get<std::string>() : std::string();
    }

    bool fetchLatestTag(const std::string& owner, const std::string& repo, std::string& tag, std::string& error)
    {
        GithubRef ref;
        ref.owner = owner;
        ref.repo = repo;
        json release;
        if (!fetchRelease(ref, release, error)) return false;
        tag = release.at("tag_name").get<std::string>();
        return true;
    }

    Resolved resolve(const Link& link)
    {
        GithubRef ref;
        if (parseGithubUrl(link.url, ref) && !ref.directAsset) {
            json release;
            Resolved r;
            if (!fetchRelease(ref, release, r.error)) return r;
            return resolveFromRelease(release, link);
        }
        Resolved r;
        r.ok = true;
        r.url = link.url;
        r.fileName = fileNameFromUrl(link.url);
        return r;
    }

    json cfwPacks()
    {
        static json cache = json::object();
        static bool done = false;
        if (done) return cache;
        done = true;

        for (const auto& link : load()) {
            if (link.kind != Kind::cfw) continue;

            GithubRef ref;
            if (parseGithubUrl(link.url, ref) && !ref.directAsset) {
                json releases = json::array();
                download::getRequest("https://api.github.com/repos/" + ref.owner + "/" + ref.repo + "/releases?per_page=10", releases, githubHeaders());
                const json packs = packsFromReleases(releases, link);
                for (auto it = packs.begin(); it != packs.end(); ++it) cache[it.key()] = it.value();
            }
            else {
                cache[link.name + " [PACK]"] = link.url;
            }
        }
        return cache;
    }

    int checkForUpdates(const std::vector<Link>& links)
    {
        static bool checked = false;
        if (!checked) {
            checked = true;
            int requests = 0;
            const int maxChecks = ghauth::isLoggedIn() ? MAX_UPDATE_CHECKS_LOGGED_IN : MAX_UPDATE_CHECKS_ANONYMOUS;
            for (const auto& link : links) {
                if (link.kind == Kind::cfw) continue;  // packs are listed with all their releases instead
                GithubRef ref;
                if (!parseGithubUrl(link.url, ref) || ref.directAsset || !ref.tag.empty()) continue;
                if (++requests > maxChecks) break;

                json release;
                std::string error;
                if (fetchRelease(ref, release, error)) {
                    latestTags()[link.url] = release.at("tag_name").get<std::string>();
                }
                else if (error == "network" || error == "rate_limit") {
                    break;  // no point in hammering a dead connection / exhausted API quota
                }
            }
        }

        int updates = 0;
        for (const auto& link : links) {
            if (updateAvailable(link)) updates++;
        }
        return updates;
    }

    std::string latestKnownTag(const Link& link)
    {
        auto it = latestTags().find(link.url);
        return it == latestTags().end() ? std::string() : it->second;
    }

    bool updateAvailable(const Link& link)
    {
        const std::string latest = latestKnownTag(link);
        return !latest.empty() && !link.installedTag.empty() && latest != link.installedTag;
    }

}  // namespace userlinks
