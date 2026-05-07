#include "gb_mod.hpp"
#include "download.hpp"
#include "constants.hpp"

#include <fmt/core.h>
#include <regex>

static std::string removeHtmlTags(const std::string& html)
{
    std::string result = std::regex_replace(html, std::regex("<[^>]+>"), "");
    result = std::regex_replace(result, std::regex("&amp;"), "&");
    result = std::regex_replace(result, std::regex("&lt;"), "<");
    result = std::regex_replace(result, std::regex("&gt;"), ">");
    result = std::regex_replace(result, std::regex("&quot;"), "\"");
    result = std::regex_replace(result, std::regex("&#039;"), "'");
    result = std::regex_replace(result, std::regex("\r\n|\r"), "\n");
    return result;
}

// GBFile

GBFile::GBFile(const std::string& name, int size, const std::string& url,
               const std::string& checkSum, const std::string& modName,
               int date, const std::string& fileID, const GBGame& game)
    : name(name), size(size), url(url), checkSum(checkSum),
      modName(modName), date(date), fileID(fileID), game(game)
{
    path = fmt::format("{}{}", DOWNLOAD_PATH, name);
}

bool GBFile::findRomfsRecursive(const nlohmann::ordered_json& obj)
{
    for (const auto& item : obj.items()) {
        if (item.key() == "romfs" || item.key() == "exefs" || item.key() == "exefs_patches")
            return true;
        if (item.value().is_object()) {
            if (findRomfsRecursive(item.value()))
                return true;
        }
    }
    return false;
}

void GBFile::loadFile()
{
    nlohmann::ordered_json file_json;
    download::getRequest(
        fmt::format("https://gamebanana.com/apiv11/File/{}", fileID),
        file_json, {"accept: application/json"});

    if (file_json.empty())
        return;

    nlohmann::ordered_json archiveFileTree = file_json;
    if (file_json.contains("_aArchiveFileTree") && file_json["_aArchiveFileTree"].is_object())
        archiveFileTree = file_json["_aArchiveFileTree"];
    else if (file_json.contains("_aMetadata") &&
             file_json["_aMetadata"].contains("_aArchiveFileTree") &&
             file_json["_aMetadata"]["_aArchiveFileTree"].is_object())
        archiveFileTree = file_json["_aMetadata"]["_aArchiveFileTree"];

    for (const auto& item : archiveFileTree.items()) {
        if (item.value().is_object() && findRomfsRecursive(item.value())) {
            romfs = true;
            break;
        }
    }
}

// GBMod

GBMod::GBMod(const std::string& name, int id, const std::string& author, const GBGame& game)
    : name(name), id(id), author(author), game(game) {}

void GBMod::loadMod()
{
    nlohmann::ordered_json mod_json;
    download::getRequest(
        fmt::format("https://gamebanana.com/apiv11/Mod/{}?_csvProperties=_sText,_aFiles,_aPreviewMedia", id),
        mod_json, {"accept: application/json"});

    if (mod_json.empty())
        return;

    try {
        description = removeHtmlTags(mod_json.at("_sText").get<std::string>());

        for (auto& file : mod_json.at("_aFiles")) {
            std::string fname = file.at("_sFile");
            std::string furl = file.at("_sDownloadUrl");
            int fsize = file.at("_nFilesize");
            std::string fchecksum = file.at("_sMd5Checksum");
            int fdate = file.at("_tsDateAdded");
            std::string fid = std::to_string(file.at("_idRow").get<int>());
            files.push_back(GBFile(fname, fsize, furl, fchecksum, name, fdate, fid, game));
        }
    }
    catch (...) {}
}

// GBModList

GBModList::GBModList(const GBGame& game, int startPage, const std::string& search)
    : game(game), currentPage(startPage), currentSearch(search)
{
    updatePage();
}

void GBModList::updatePage()
{
    nlohmann::ordered_json mod_json;

    std::string url;
    if (!currentCategory.getName().empty()) {
        url = fmt::format(
            "https://gamebanana.com/apiv11/Mod/Index?_nPerpage=15&_aFilters[Generic_Category]={}&_nPage={}",
            currentCategory.getID(), currentPage);
    }
    else if (currentSearch.empty()) {
        url = fmt::format(
            "https://gamebanana.com/apiv11/Game/{}/Subfeed?_nPage={}&_nPerpage=15&_csvModelInclusions=Mod",
            game.getGamebananaID(), currentPage);
    }
    else {
        url = fmt::format(
            "https://gamebanana.com/apiv11/Game/{}/Subfeed?_nPage={}&_nPerpage=15&_sName={}&_csvModelInclusions=Mod",
            game.getGamebananaID(), currentPage, currentSearch);
    }

    download::getRequest(url, mod_json, {"accept: application/json"});

    if (mod_json.empty())
        return;

    mods.clear();
    try {
        for (auto& mod : mod_json.at("_aRecords")) {
            std::string mname = mod.at("_sName");
            int mid = mod.at("_idRow");
            std::string mauthor = mod.at("_aSubmitter").at("_sName");
            mods.push_back(GBMod(mname, mid, mauthor, game));
        }
    }
    catch (...) {}
}

