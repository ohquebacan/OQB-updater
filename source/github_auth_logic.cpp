#include "github_auth.hpp"

namespace ghauth {

    namespace {
        std::string str(const nlohmann::ordered_json& o, const char* key)
        {
            if (!o.is_object()) return "";
            auto it = o.find(key);
            return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string();
        }
        int num(const nlohmann::ordered_json& o, const char* key, int fallback)
        {
            if (!o.is_object()) return fallback;
            auto it = o.find(key);
            return (it != o.end() && it->is_number_integer()) ? it->get<int>() : fallback;
        }
    }  // namespace

    DeviceCode parseDeviceCode(const nlohmann::ordered_json& json)
    {
        DeviceCode d;
        d.deviceCode = str(json, "device_code");
        d.userCode = str(json, "user_code");
        const std::string uri = str(json, "verification_uri");
        if (!uri.empty()) d.verificationUri = uri;
        d.interval = num(json, "interval", 5);
        if (d.interval < 5) d.interval = 5;
        d.expiresIn = num(json, "expires_in", 900);
        d.ok = !d.deviceCode.empty() && !d.userCode.empty();
        if (!d.ok) d.error = str(json, "error");
        return d;
    }

    PollResult parsePoll(const nlohmann::ordered_json& json)
    {
        PollResult r;
        const std::string token = str(json, "access_token");
        if (!token.empty()) {
            r.state = Poll::granted;
            r.token = token;
            return r;
        }
        const std::string error = str(json, "error");
        if (error == "authorization_pending") r.state = Poll::pending;
        else if (error == "slow_down") r.state = Poll::slowDown;
        else if (error == "access_denied") r.state = Poll::denied;
        else if (error == "expired_token") r.state = Poll::expired;
        else r.state = Poll::error;
        return r;
    }

}  // namespace ghauth
