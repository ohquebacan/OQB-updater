#include <sys/stat.h>

#include <ctime>
#include <string>

#include "fs.hpp"
#include "github_auth.hpp"
#include "installed_updates.hpp"

namespace updates {

    namespace {
        constexpr const char CATALOG_PATH[] = "/config/aio-switch-updater/updates_catalog.json";
        constexpr const char CACHE_PATH[] = "/config/aio-switch-updater/updates_cache.json";
        constexpr const char CACHE_DIR[] = "/config/aio-switch-updater/";
        constexpr long CACHE_SECONDS_LOGGED_IN = 6 * 3600;
        constexpr long CACHE_SECONDS_ANONYMOUS = 24 * 3600;
        bool g_rateLimited = false;
        constexpr int MAX_REQUESTS = 25;

        using json = nlohmann::ordered_json;

        bool exists(const std::string& path)
        {
            struct stat st;
            return stat(path.c_str(), &st) == 0;
        }
    }  // namespace

    std::vector<Entry> loadCatalog()
    {
        return mergeCatalogs(builtinCatalog(), entriesFromJson(fs::parseJsonFile(CATALOG_PATH)));
    }

    bool rateLimited()
    {
        return g_rateLimited;
    }

    std::vector<Item> scan(bool allowNetwork)
    {
        const long cacheSeconds = ghauth::isLoggedIn() ? CACHE_SECONDS_LOGGED_IN : CACHE_SECONDS_ANONYMOUS;
        std::vector<Item> items;
        for (const auto& e : loadCatalog()) {
            for (const auto& p : e.detect) {
                if (exists(p)) {
                    Item it;
                    it.entry = e;
                    it.foundPath = p;
                    items.push_back(std::move(it));
                    break;
                }
            }
        }

        json cache = fs::parseJsonFile(CACHE_PATH);
        if (!cache.is_object()) cache = json::object();
        const long now = static_cast<long>(std::time(nullptr));
        int requests = 0;
        bool cacheChanged = false;
        bool offline = false;

        for (auto& it : items) {
            const std::string url = "https://github.com/" + it.entry.repo;

            // which version is on the card?
            const std::string ext = it.foundPath.size() > 4 ? it.foundPath.substr(it.foundPath.size() - 4) : "";
            if (ext == ".nro" || ext == ".ovl") it.installed = nroVersion(it.foundPath);
            if (normalizeVersion(it.installed).empty()) it.installed = userlinks::recordedTag(url);

            // newest release: cache first
            auto c = cache.find(it.entry.repo);
            if (c != cache.end() && c->is_object() && c->contains("tag") && c->contains("time")) {
                const long age = now - c->at("time").get<long>();
                if (age >= 0 && age < cacheSeconds) it.latest = c->at("tag").get<std::string>();
            }
            if (it.latest.empty() && allowNetwork && !offline && requests < MAX_REQUESTS) {
                requests++;
                const size_t slash = it.entry.repo.find('/');
                std::string tag, error;
                if (userlinks::fetchLatestTag(it.entry.repo.substr(0, slash), it.entry.repo.substr(slash + 1), tag, error)) {
                    it.latest = tag;
                    cache[it.entry.repo] = {{"tag", tag}, {"time", now}};
                    cacheChanged = true;
                }
                else if (error == "network" || error == "rate_limit") {
                    offline = true;  // stop asking: nothing will come back
                    if (error == "rate_limit") g_rateLimited = true;
                }
            }
            it.status = computeStatus(it.installed, it.latest);
        }

        if (cacheChanged) {
            fs::createTree(CACHE_DIR);
            fs::writeJsonToFile(cache, CACHE_PATH);
        }
        return items;
    }

}  // namespace updates
