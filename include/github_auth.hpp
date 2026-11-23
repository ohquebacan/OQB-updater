#pragma once

#include <json.hpp>
#include <string>
#include <vector>

// Optional GitHub sign-in (OAuth "device flow"): with it the API allows 5000 requests
// per hour instead of 60. The token only reads public data.
namespace ghauth {

    constexpr const char CLIENT_ID[] = "Ov23liQtsQRNCzCS3fze";

    struct DeviceCode
    {
        bool ok = false;
        std::string deviceCode;
        std::string userCode;
        std::string verificationUri = "https://github.com/login/device";
        int interval = 5;
        int expiresIn = 900;
        std::string error;
    };

    enum class Poll
    {
        pending,
        slowDown,
        granted,
        denied,
        expired,
        error,
    };

    struct PollResult
    {
        Poll state = Poll::error;
        std::string token;
    };

    // ---------------------------------------------------------------- pure logic
    DeviceCode parseDeviceCode(const nlohmann::ordered_json& json);
    PollResult parsePoll(const nlohmann::ordered_json& json);

    // ------------------------------------------------------- storage + network
    bool isLoggedIn();
    void logout();
    // Headers for api.github.com requests (adds the token when signed in).
    std::vector<std::string> apiHeaders();

    DeviceCode requestCode();                          // step 1: ask GitHub for a code
    PollResult pollOnce(const std::string& deviceCode);  // step 2: has the user confirmed? stores the token on success

}  // namespace ghauth
