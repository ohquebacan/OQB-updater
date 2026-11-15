#include <cstdio>
#include <cstdint>
#include <cstring>

#include "installed_updates.hpp"

namespace updates {

    std::string normalizeVersion(const std::string& raw)
    {
        size_t start = raw.find_first_of("0123456789");
        if (start == std::string::npos) return "";
        std::string out;
        for (size_t i = start; i < raw.size(); i++) {
            const char c = raw[i];
            if ((c >= '0' && c <= '9') || c == '.') out += c;
            else break;
        }
        while (!out.empty() && out.back() == '.') out.pop_back();
        return out;
    }

    namespace {
        std::vector<unsigned long> parts(const std::string& v)
        {
            std::vector<unsigned long> p;
            unsigned long cur = 0;
            bool have = false;
            for (char c : v) {
                if (c == '.') {
                    p.push_back(cur);
                    cur = 0;
                    have = false;
                }
                else {
                    cur = cur * 10 + static_cast<unsigned long>(c - '0');
                    have = true;
                }
            }
            if (have) p.push_back(cur);
            return p;
        }
    }  // namespace

    int compareVersions(const std::string& a, const std::string& b)
    {
        const std::string na = normalizeVersion(a), nb = normalizeVersion(b);
        if (na.empty() || nb.empty()) return 2;
        auto pa = parts(na), pb = parts(nb);
        const size_t n = pa.size() > pb.size() ? pa.size() : pb.size();
        for (size_t i = 0; i < n; i++) {
            const unsigned long x = i < pa.size() ? pa[i] : 0;
            const unsigned long y = i < pb.size() ? pb[i] : 0;
            if (x < y) return -1;
            if (x > y) return 1;
        }
        return 0;
    }

    std::string parentDir(const std::string& path)
    {
        const size_t slash = path.find_last_of('/');
        if (slash == std::string::npos) return "/";
        return path.substr(0, slash + 1);
    }

    Status computeStatus(const std::string& installed, const std::string& latest)
    {
        if (installed.empty() || latest.empty()) return Status::unknown;
        const int c = compareVersions(installed, latest);
        if (c == -1) return Status::updateAvailable;
        if (c == 0 || c == 1) return Status::upToDate;
        // not comparable as numbers: only a different text counts as "update"
        return installed == latest ? Status::upToDate : Status::updateAvailable;
    }

    std::vector<Entry> builtinCatalog()
    {
        auto app = [](const char* name, const char* repo, std::vector<std::string> detect, const char* filter = "") {
            Entry e;
            e.name = name;
            e.repo = repo;
            e.detect = std::move(detect);
            e.filter = filter;
            return e;
        };
        auto zip = [](const char* name, const char* repo, const char* detect) {
            Entry e;
            e.name = name;
            e.repo = repo;
            e.detect = {detect};
            e.extract = true;
            e.extractTo = "/";
            return e;
        };
        // folder-based apps: a release may be a plain .nro or a zip (SD layout or app-only)
        auto folder = [](const char* name, const char* repo, const char* detect, const char* dir) {
            Entry e;
            e.name = name;
            e.repo = repo;
            e.detect = {detect};
            e.installDir = dir;
            e.autoLayout = true;
            return e;
        };
        // overlay: a plain .ovl or a zip (maybe with its sysmodule in SD layout)
        auto overlay = [](const char* name, const char* repo, const char* detect) {
            Entry e;
            e.name = name;
            e.repo = repo;
            e.detect = {detect};
            e.installDir = "/switch/.overlays/";
            e.autoLayout = true;
            return e;
        };
        return {
            folder("FlashNX", "Jonathan8520/FlashNX", "/switch/FlashNX", "/switch/FlashNX/"),
            folder("melonDS", "melonDS-emu/melonDS", "/switch/melonds", "/switch/melonds/"),
            folder("Homebrew-Manager (HomeTile)", "Amir-Ehsani/HomeTile-switch", "/switch/Homebrew-Manager", "/switch/Homebrew-Manager/"),
            folder("SwitchU", "PoloNX/SwitchU", "/switch/SwitchU", "/switch/SwitchU/"),
            folder("CustomBootScreen", "esteban-cz/CustomBootScreen", "/switch/CustomBootScreen", "/switch/CustomBootScreen/"),
            folder("AmiiboGenerator", "Slluxx/AmiiboGenerator", "/switch/AmiiboGenerator", "/switch/AmiiboGenerator/"),
            app("nsp-forwarder", "TooTallNate/switch-nsp-forwarder", {"/switch/nsp-forwarder.nro"}, ".nro"),
            app("SaltyNX-Tool", "masagrator/SaltyNX-Tool", {"/switch/SaltyNX-Tool.nro"}, ".nro"),
            overlay("homenx achievements", "ad3333m/homenx", "/switch/.overlays/homenx-achievements.ovl"),
            overlay("Horizon OC", "Horizon-OC/Horizon-OC", "/switch/.overlays/horizon-oc-overlay.ovl"),
            overlay("EdiZon Overlay", "proferabg/EdiZon-Overlay", "/switch/.overlays/ovlEdiZon.ovl"),
            app("DBI", "rashevskyv/dbi", {"/switch/DBI/DBI.nro", "/switch/DBI.nro"}, "DBI.nro"),
            app("Checkpoint", "FlagBrew/Checkpoint", {"/switch/Checkpoint/Checkpoint.nro", "/switch/Checkpoint.nro"}, ".nro"),
            app("JKSV", "J-D-K/JKSV", {"/switch/JKSV/JKSV.nro", "/switch/JKSV.nro"}, ".nro"),
            app("Goldleaf", "XorTroll/Goldleaf", {"/switch/Goldleaf/Goldleaf.nro", "/switch/Goldleaf.nro"}, ".nro"),
            app("NX-Shell", "joel16/NX-Shell", {"/switch/NX-Shell/NX-Shell.nro", "/switch/NX-Shell.nro"}, ".nro"),
            app("NXThemes Installer", "exelix11/SwitchThemeInjector", {"/switch/NXThemesInstaller/NXThemesInstaller.nro", "/switch/NXThemesInstaller.nro"}, "NXThemesInstaller.nro"),
            app("Ultrahand Overlay", "ppkantorski/Ultrahand-Overlay", {"/switch/.overlays/ovlmenu.ovl"}, "ovlmenu.ovl"),
            app("Status Monitor Overlay", "masagrator/Status-Monitor-Overlay", {"/switch/.overlays/Status-Monitor-Overlay.ovl"}, ".ovl"),
            app("ovl-sysmodules", "WerWolv/ovl-sysmodules", {"/switch/.overlays/ovl-sysmodules.ovl"}, ".ovl"),
            zip("Awoo Installer", "Huntereb/Awoo-Installer", "/switch/awoo-installer"),
            zip("sys-con (Controller)", "cathery/sys-con", "/atmosphere/contents/690000000000000D"),
            zip("ldn_mitm", "DefenderOfHyrule/ldn_mitm", "/atmosphere/contents/4200000000000010"),
            zip("sys-ftpd-light", "cathery/sys-ftpd-light", "/atmosphere/contents/420000000000000E"),
            zip("MissionControl", "ndeadly/MissionControl", "/atmosphere/contents/010000000000bd00"),
            zip("emuiibo", "XorTroll/emuiibo", "/atmosphere/contents/0100000000000352"),
        };
    }

    std::vector<Entry> entriesFromJson(const nlohmann::ordered_json& json)
    {
        std::vector<Entry> out;
        if (!json.is_array()) return out;
        auto str = [](const nlohmann::ordered_json& o, const char* k) {
            auto it = o.find(k);
            return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string();
        };
        for (const auto& o : json) {
            if (!o.is_object()) continue;
            Entry e;
            e.name = str(o, "name");
            e.repo = str(o, "repo");
            e.filter = str(o, "filter");
            auto d = o.find("detect");
            if (d != o.end()) {
                if (d->is_string()) e.detect.push_back(d->get<std::string>());
                else if (d->is_array())
                    for (const auto& p : *d)
                        if (p.is_string()) e.detect.push_back(p.get<std::string>());
            }
            auto x = o.find("extract");
            e.extract = (x != o.end() && x->is_boolean() && x->get<bool>());
            const std::string to = userlinks::normalizeDir(str(o, "extract_to"));
            if (!to.empty()) e.extractTo = to;
            const std::string inst = userlinks::normalizeDir(str(o, "install_dir"));
            if (!inst.empty()) e.installDir = inst;
            auto au = o.find("auto_layout");
            e.autoLayout = (au != o.end() && au->is_boolean() && au->get<bool>());
            // needs a name, a repo of the form owner/repo and at least one absolute detect path
            if (e.name.empty() || e.detect.empty()) continue;
            const size_t slash = e.repo.find('/');
            if (slash == std::string::npos || slash == 0 || slash + 1 >= e.repo.size() || e.repo.find('/', slash + 1) != std::string::npos) continue;
            bool ok = true;
            for (auto& p : e.detect) {
                if (p.empty() || p[0] != '/' || p.find("..") != std::string::npos) ok = false;
            }
            if (ok) out.push_back(std::move(e));
        }
        return out;
    }

    std::vector<Entry> mergeCatalogs(std::vector<Entry> base, const std::vector<Entry>& extra)
    {
        for (const auto& e : extra) {
            bool replaced = false;
            for (auto& b : base) {
                if (b.name == e.name) {
                    b = e;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) base.push_back(e);
        }
        return base;
    }

    namespace {
        bool readAt(FILE* f, long off, void* buf, size_t n)
        {
            return std::fseek(f, off, SEEK_SET) == 0 && std::fread(buf, 1, n, f) == n;
        }
        uint32_t le32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
        uint64_t le64(const unsigned char* p) { return le32(p) | (static_cast<uint64_t>(le32(p + 4)) << 32); }
    }  // namespace

    std::string nroVersion(const std::string& path)
    {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return "";
        std::string version;
        unsigned char hdr[0x20];
        if (readAt(f, 0, hdr, sizeof(hdr)) && std::memcmp(hdr + 0x10, "NRO0", 4) == 0) {
            const uint32_t nroSize = le32(hdr + 0x18);
            unsigned char asset[0x38];
            // asset section: "ASET", version, icon{off,size}, nacp{off,size}, romfs{off,size}
            if (nroSize > 0 && readAt(f, static_cast<long>(nroSize), asset, sizeof(asset)) && std::memcmp(asset, "ASET", 4) == 0) {
                const uint64_t nacpOff = le64(asset + 0x18);
                const uint64_t nacpSize = le64(asset + 0x20);
                if (nacpSize >= 0x3070 && nacpOff < 0x40000000) {
                    char v[0x11] = {0};
                    if (readAt(f, static_cast<long>(nroSize + nacpOff + 0x3060), v, 0x10)) version = v;
                }
            }
        }
        std::fclose(f);
        return version;
    }

    userlinks::Link toLink(const Item& item)
    {
        userlinks::Link l;
        l.name = item.entry.name;
        l.url = "https://github.com/" + item.entry.repo;
        l.filter = item.entry.filter;
        l.extract = item.entry.extract;
        l.extractTo = item.entry.extractTo;
        l.kind = item.entry.extract ? userlinks::Kind::mod : userlinks::Kind::app;
        // a single file replaces the one that is installed, wherever the user put it
        l.saveTo = item.entry.installDir.empty() ? parentDir(item.foundPath.empty() ? item.entry.detect.front() : item.foundPath)
                                                 : item.entry.installDir;
        l.autoLayout = item.entry.autoLayout;
        if (l.autoLayout && l.extract) l.extractTo = l.saveTo;  // fallback when the zip has no SD folders
        return l;
    }

}  // namespace updates
