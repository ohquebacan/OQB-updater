#include <algorithm>
#include <cctype>

#include "user_links.hpp"

namespace userlinks {

    namespace {
        using json = nlohmann::ordered_json;

        std::string lower(std::string s)
        {
            for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        std::string trim(const std::string& s)
        {
            size_t b = 0, e = s.size();
            while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) b++;
            while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
            return s.substr(b, e - b);
        }

        bool endsWith(const std::string& s, const std::string& suffix)
        {
            return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        std::string str(const json& j, const char* key)
        {
            auto it = j.find(key);
            return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string();
        }

        std::vector<std::string> splitPath(const std::string& s)
        {
            std::vector<std::string> parts;
            std::string cur;
            for (char c : s) {
                if (c == '/') {
                    if (!cur.empty()) parts.push_back(cur);
                    cur.clear();
                }
                else {
                    cur += c;
                }
            }
            if (!cur.empty()) parts.push_back(cur);
            return parts;
        }

        bool validRepoPart(const std::string& s)
        {
            if (s.empty()) return false;
            for (unsigned char c : s) {
                if (!(std::isalnum(c) || c == '-' || c == '_' || c == '.')) return false;
            }
            return true;
        }

        std::string sanitizeFileName(std::string name)
        {
            for (auto& c : name) {
                if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || static_cast<unsigned char>(c) < 0x20)
                    c = '_';
            }
            return name;
        }

        bool isSkippableAsset(const std::string& lowerName)
        {
            for (const char* ext : {".sha256", ".sha1", ".md5", ".sig", ".asc", ".txt", ".md", ".sum"}) {
                if (endsWith(lowerName, ext)) return true;
            }
            return false;
        }
    }  // namespace

    std::string kindKey(Kind kind)
    {
        switch (kind) {
            case Kind::app: return "app";
            case Kind::theme: return "theme";
            case Kind::cheats: return "cheats";
            case Kind::mod: return "mod";
            case Kind::other: return "other";
            case Kind::cfw: return "cfw";
        }
        return "other";
    }

    Kind kindFromKey(const std::string& key)
    {
        const std::string k = lower(trim(key));
        if (k == "app" || k == "nro") return Kind::app;
        if (k == "theme") return Kind::theme;
        if (k == "cheats") return Kind::cheats;
        if (k == "mod") return Kind::mod;
        if (k == "cfw") return Kind::cfw;
        return Kind::other;
    }

    Defaults defaultsFor(Kind kind)
    {
        switch (kind) {
            case Kind::app: return {"/switch/", false, "/"};
            case Kind::theme: return {"/themes/", false, "/themes/"};
            case Kind::cheats: return {"/atmosphere/contents/", true, "/atmosphere/contents/"};
            case Kind::mod: return {"/", true, "/"};
            case Kind::other: return {"/downloads/", false, "/"};
            case Kind::cfw: return {"/", true, "/"};
        }
        return {"/", false, "/"};
    }

    std::string normalizeDir(const std::string& path)
    {
        std::string s = trim(path);
        for (auto& c : s) {
            if (c == '\\') c = '/';
        }
        if (lower(s).rfind("sdmc:", 0) == 0) s = s.substr(5);
        if (s.empty()) return "";

        for (unsigned char c : s) {
            if (c < 0x20 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                return "";
        }

        std::string result = "/";
        for (const auto& part : splitPath(s)) {
            if (part == "..") return "";
            if (part == ".") continue;
            if (part.back() == ' ') return "";
            result += part + "/";
        }
        return result;
    }

    bool parseGithubUrl(const std::string& urlIn, GithubRef& out)
    {
        out = GithubRef{};
        std::string url = trim(urlIn);
        const size_t cut = url.find_first_of("?#");
        if (cut != std::string::npos) url = url.substr(0, cut);

        const std::string l = lower(url);
        size_t pos = std::string::npos;
        for (const std::string prefix : {"https://www.github.com/", "http://www.github.com/", "https://github.com/", "http://github.com/", "www.github.com/", "github.com/"}) {
            if (l.rfind(prefix, 0) == 0) {
                pos = prefix.size();
                break;
            }
        }
        if (pos == std::string::npos) return false;

        const auto seg = splitPath(url.substr(pos));
        if (seg.size() < 2) return false;

        std::string repo = seg[1];
        if (endsWith(lower(repo), ".git")) repo = repo.substr(0, repo.size() - 4);
        if (!validRepoPart(seg[0]) || !validRepoPart(repo)) return false;

        out.owner = seg[0];
        out.repo = repo;
        if (seg.size() >= 3) {
            if (seg[2] == "releases") {
                if (seg.size() >= 4 && seg[3] == "download")
                    out.directAsset = true;
                else if (seg.size() >= 5 && seg[3] == "tag")
                    out.tag = seg[4];
            }
            else if (seg[2] == "archive" || seg[2] == "raw") {
                out.directAsset = true;
            }
        }
        return true;
    }

    bool isGithubRepoLink(const Link& link)
    {
        GithubRef ref;
        return parseGithubUrl(link.url, ref) && !ref.directAsset;
    }

    std::string fileNameFromUrl(const std::string& urlIn)
    {
        std::string url = urlIn;
        const size_t cut = url.find_first_of("?#");
        if (cut != std::string::npos) url = url.substr(0, cut);
        while (!url.empty() && url.back() == '/') url.pop_back();
        const size_t slash = url.rfind('/');
        std::string name = slash == std::string::npos ? url : url.substr(slash + 1);

        std::string decoded;
        for (size_t i = 0; i < name.size(); i++) {
            if (name[i] == '%' && i + 2 < name.size() + 0 && std::isxdigit(static_cast<unsigned char>(name[i + 1])) && std::isxdigit(static_cast<unsigned char>(name[i + 2]))) {
                decoded += static_cast<char>(std::stoi(name.substr(i + 1, 2), nullptr, 16));
                i += 2;
            }
            else {
                decoded += name[i];
            }
        }
        decoded = sanitizeFileName(trim(decoded));
        return decoded.empty() ? "download" : decoded;
    }

    std::string defaultNameFor(const std::string& url)
    {
        GithubRef ref;
        if (parseGithubUrl(url, ref)) return ref.repo;
        std::string name = fileNameFromUrl(url);
        const size_t dot = name.rfind('.');
        if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
        return name;
    }

    std::vector<Asset> assetsFromRelease(const json& release)
    {
        std::vector<Asset> assets;
        if (!release.is_object()) return assets;
        auto it = release.find("assets");
        if (it == release.end() || !it->is_array()) return assets;
        for (const auto& a : *it) {
            if (!a.is_object()) continue;
            Asset asset{str(a, "name"), str(a, "browser_download_url")};
            if (!asset.name.empty() && !asset.url.empty()) assets.push_back(asset);
        }
        return assets;
    }

    bool chooseAsset(const std::vector<Asset>& assets, const Link& link, Asset& out)
    {
        const std::string needle = lower(trim(link.filter));
        std::vector<Asset> pool;
        for (const auto& a : assets) {
            if (needle.empty() || lower(a.name).find(needle) != std::string::npos) pool.push_back(a);
        }
        if (pool.empty()) return false;

        std::vector<std::string> prefer;
        if (link.extract)
            prefer = {".zip"};
        else if (link.kind == Kind::app)
            prefer = {".nro", ".zip", ".ovl"};
        else if (link.kind == Kind::theme)
            prefer = {".nxtheme", ".zip"};
        else
            prefer = {".zip"};

        for (const auto& ext : prefer) {
            for (const auto& a : pool) {
                if (endsWith(lower(a.name), ext)) {
                    out = a;
                    return true;
                }
            }
        }

        // "other" files (or an explicit filter) may be anything except checksums/signatures.
        if (link.kind == Kind::other || !needle.empty()) {
            for (const auto& a : pool) {
                if (!isSkippableAsset(lower(a.name))) {
                    out = a;
                    return true;
                }
            }
        }
        return false;
    }

    Resolved resolveFromRelease(const json& release, const Link& link)
    {
        Resolved r;
        r.tag = release.is_object() ? str(release, "tag_name") : std::string();
        const auto assets = assetsFromRelease(release);
        if (assets.empty()) {
            r.error = "no_assets";
            return r;
        }
        Asset chosen;
        if (!chooseAsset(assets, link, chosen)) {
            r.error = "no_match";
            return r;
        }
        r.ok = true;
        r.url = chosen.url;
        r.fileName = sanitizeFileName(chosen.name);
        return r;
    }

    json linksToJson(const std::vector<Link>& links)
    {
        json arr = json::array();
        for (const auto& l : links) {
            json o = json::object();
            o["name"] = l.name;
            o["url"] = l.url;
            o["kind"] = kindKey(l.kind);
            o["save_to"] = l.saveTo;
            o["extract"] = l.extract;
            o["extract_to"] = l.extractTo;
            o["filter"] = l.filter;
            o["installed_tag"] = l.installedTag;
            arr.push_back(o);
        }
        return arr;
    }

    std::vector<Link> linksFromJson(const json& j)
    {
        std::vector<Link> links;
        if (!j.is_array()) return links;
        for (const auto& o : j) {
            if (!o.is_object()) continue;
            Link l;
            l.name = trim(str(o, "name"));
            l.url = trim(str(o, "url"));
            if (l.url.empty()) continue;
            if (l.name.empty()) l.name = defaultNameFor(l.url);
            l.kind = kindFromKey(str(o, "kind"));
            const Defaults d = defaultsFor(l.kind);
            const std::string save = normalizeDir(str(o, "save_to"));
            l.saveTo = save.empty() ? d.saveTo : save;
            auto ex = o.find("extract");
            l.extract = (ex != o.end() && ex->is_boolean()) ? ex->get<bool>() : d.extract;
            const std::string extractTo = normalizeDir(str(o, "extract_to"));
            l.extractTo = extractTo.empty() ? d.extractTo : extractTo;
            l.filter = trim(str(o, "filter"));
            l.installedTag = str(o, "installed_tag");
            links.push_back(l);
        }
        return links;
    }

    json packsFromReleases(const json& releases, const Link& link)
    {
        json packs = json::object();
        if (!releases.is_array()) return packs;
        int used = 0;
        for (const auto& release : releases) {
            if (!release.is_object() || used >= 10) continue;
            auto draft = release.find("draft");
            if (draft != release.end() && draft->is_boolean() && draft->get<bool>()) continue;

            std::string base = str(release, "name");
            if (base.empty()) base = str(release, "tag_name");
            if (base.empty()) continue;
            const std::string label = lower(base).find(lower(link.name)) != std::string::npos ? base : link.name + " " + base;

            std::vector<Asset> zips;
            for (const auto& a : assetsFromRelease(release)) {
                if (endsWith(lower(a.name), ".zip")) zips.push_back(a);
            }
            if (zips.empty()) continue;
            used++;
            for (const auto& z : zips) {
                packs[(zips.size() > 1 ? label + " - " + z.name : label) + " [PACK]"] = z.url;
            }
        }
        return packs;
    }

}  // namespace userlinks
