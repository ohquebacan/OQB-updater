#include "gb_game.hpp"
#include "download.hpp"

#include <curl/curl.h>
#include <fmt/core.h>

GBGame::GBGame(const std::string& m_title, const std::string& m_tid)
{
    title = m_title;
    tid = m_tid;
    searchGame();
    parseJson();
    if (gamebananaID > 0)
        loadCategories();
}

void GBGame::searchGame()
{
    auto curl = curl_easy_init();
    std::string title_url = curl_easy_escape(curl, title.c_str(), title.length());
    curl_easy_cleanup(curl);

    std::string url = fmt::format(
        "https://gamebanana.com/apiv11/Util/Search/Results?_sModelName=Game&_sOrder=best_match&_sSearchString={}%20%28Switch%29",
        title_url);

    download::getRequest(url, json, {"accept: application/json"});
}

void GBGame::parseJson()
{
    try {
        if (json.empty() || json.at("_aMetadata").at("_nRecordCount").get<int>() == 0)
            return;

        const auto& records = json.at("_aRecords");

        // Extract first word of the title to use as matching keyword
        std::string firstWord = title.substr(0, title.find(' '));
        if (firstWord.empty()) firstWord = title;

        int pos = -1;
        for (size_t i = 0; i < records.size(); ++i) {
            std::string sName = records[i].at("_sName").get<std::string>();
            // Skip generic platform pages
            if (sName == "Nintendo Switch" || sName == "Switch" || sName == "Nintendo 3DS")
                continue;
            // Accept first result containing the first keyword of our title
            if (sName.find(firstWord) != std::string::npos) {
                pos = i;
                break;
            }
        }

        if (pos == -1)
            return; // No good match found, leave gamebananaID = 0

        const auto& record = records[pos];
        title = record.at("_sName").get<std::string>();
        gamebananaID = record.at("_idRow").get<int>();
    }
    catch (...) {
        gamebananaID = -1;
    }
}

void GBGame::loadCategories()
{
    nlohmann::ordered_json cat_json;
    download::getRequest(
        fmt::format("https://gamebanana.com/apiv11/Game/{}/ProfilePage", gamebananaID),
        cat_json, {"accept: application/json"});

    if (cat_json.empty() || cat_json.find("_aModRootCategories") == cat_json.end())
        return;

    int index = 0;
    for (auto& tag : cat_json.at("_aModRootCategories")) {
        categories.push_back(GBCategory(
            tag.at("_sName").get<std::string>(),
            tag.at("_idRow").get<int>(),
            index++));
    }
}
