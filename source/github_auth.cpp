#include "github_auth.hpp"

#include "download.hpp"
#include "fs.hpp"

namespace ghauth {

    namespace {
        constexpr const char TOKEN_FILE[] = "/config/aio-switch-updater/github_token.json";
        constexpr const char TOKEN_DIR[] = "/config/aio-switch-updater/";
        using json = nlohmann::ordered_json;

        std::string& cachedToken()
        {
            static std::string token;
            return token;
        }
        bool& loaded()
        {
            static bool l = false;
            return l;
        }

        void load()
        {
            if (loaded()) return;
            loaded() = true;
            json j = fs::parseJsonFile(TOKEN_FILE);
            if (j.is_object() && j.contains("access_token") && j["access_token"].is_string()) cachedToken() = j["access_token"].get<std::string>();
        }
    }  // namespace

    bool isLoggedIn()
    {
        load();
        return !cachedToken().empty();
    }

    void logout()
    {
        loaded() = true;
        cachedToken().clear();
        fs::createTree(TOKEN_DIR);
        json j = json::object();
        fs::writeJsonToFile(j, TOKEN_FILE);
    }

    std::vector<std::string> apiHeaders()
    {
        load();
        std::vector<std::string> h = {"accept: application/vnd.github.v3+json"};
        if (!cachedToken().empty()) h.push_back("authorization: Bearer " + cachedToken());
        return h;
    }

    DeviceCode requestCode()
    {
        json res;
        const long code = download::getRequest("https://github.com/login/device/code", res, {"accept: application/json"},
                                               std::string("client_id=") + CLIENT_ID + "&scope=");
        DeviceCode d = parseDeviceCode(res);
        if (!d.ok && d.error.empty()) d.error = (code == 0) ? "network" : "http_" + std::to_string(code);
        return d;
    }

    PollResult pollOnce(const std::string& deviceCode)
    {
        json res;
        const long code = download::getRequest("https://github.com/login/oauth/access_token", res, {"accept: application/json"},
                                               std::string("client_id=") + CLIENT_ID + "&device_code=" + deviceCode +
                                                   "&grant_type=urn:ietf:params:oauth:grant-type:device_code");
        PollResult r = parsePoll(res);
        if (code == 0) r.state = Poll::error;
        if (r.state == Poll::granted) {
            loaded() = true;
            cachedToken() = r.token;
            fs::createTree(TOKEN_DIR);
            json j = {{"access_token", r.token}};
            fs::writeJsonToFile(j, TOKEN_FILE);
        }
        return r;
    }

}  // namespace ghauth
