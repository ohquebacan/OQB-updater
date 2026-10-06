#include "color_swapper.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#include "constants.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "progress_event.hpp"
#include "utils.hpp"

namespace i18n = brls::i18n;
using namespace i18n::literals;

using json = nlohmann::ordered_json;

namespace {

    constexpr const char BACKUP[] = "_backup";

    int hexToBGR(const std::string& hex)
    {
        std::string R = hex.substr(0, 2);
        std::string G = hex.substr(2, 2);
        std::string B = hex.substr(4, 2);
        return std::stoi(B + G + R, 0, 16);
    }

    std::string BGRToHex(int v)
    {
        std::stringstream ss;
        v = ((v & 0xFF) << 16) + (v & 0xFF00) + (v >> 16) + 256;
        ss << std::setfill('0') << std::setw(6) << std::right << std::hex << v;
        return ss.str();
    }

    bool isHexaAnd3Bytes(const std::string& str)
    {
        if (str.size() != 6) return false;
        for (char const& c : str) {
            if (!isxdigit(c)) return false;
        }
        return true;
    }

}  // namespace

namespace JC {

    int setColor(const std::vector<int>& colors)
    {
        Result pads, ljc, rjc;
        int res = 0;
        s32 nbEntries;
        HidsysUniquePadId UniquePadIds[2] = {};

        pads = hidsysGetUniquePadsFromNpad(HidNpadIdType_Handheld, UniquePadIds, 2, &nbEntries);
        if (R_SUCCEEDED(pads)) {
            ljc = hiddbgUpdateControllerColor(colors[0], colors[1], UniquePadIds[0]);
            if (R_FAILED(ljc)) res += 1;
            rjc = hiddbgUpdateControllerColor(colors[2], colors[3], UniquePadIds[1]);
            if (R_FAILED(rjc)) res += 2;
        }
        else {
            res += 4;
        }
        return res;
    }

    int backupToJSON(json& profiles, const std::string& path)
    {
        HidNpadControllerColor color_left;
        HidNpadControllerColor color_right;
        Result res = hidGetNpadControllerColorSplit(HidNpadIdType_Handheld, &color_left, &color_right);
        std::vector<int> oldBackups;
        if (R_SUCCEEDED(res)) {
            int i = 0;
            for (const auto& x : profiles.items()) {
                if (x.value()["name"] == BACKUP) {
                    oldBackups.push_back(i);
                }
                i++;
            }
            for (auto& k : oldBackups) {
                profiles.erase(profiles.begin() + k);
            }
            json newBackup = json::object(
                {{"name", BACKUP},
                 {"L_JC", BGRToHex(color_left.main)},
                 {"L_BTN", BGRToHex(color_left.sub)},
                 {"R_JC", BGRToHex(color_right.main)},
                 {"R_BTN", BGRToHex(color_right.sub)}});
            profiles.push_back(newBackup);
            fs::writeJsonToFile(profiles, path);
            return 0;
        }
        else {
            return -1;
        }
    }

    json backupProfile()
    {
        json newBackup;
        HidNpadControllerColor color_left;
        HidNpadControllerColor color_right;
        Result res = hidGetNpadControllerColorSplit(HidNpadIdType_Handheld, &color_left, &color_right);
        if (R_SUCCEEDED(res)) {
            newBackup = json::object(
                {{"name", BACKUP},
                 {"L_JC", BGRToHex(color_left.main)},
                 {"L_BTN", BGRToHex(color_left.sub)},
                 {"R_JC", BGRToHex(color_right.main)},
                 {"R_BTN", BGRToHex(color_right.sub)}});
        }
        return newBackup;
    }

    std::deque<std::pair<std::string, std::vector<int>>> getProfiles(const std::string& path)
    {
        std::deque<std::pair<std::string, std::vector<int>>> res;
        bool properData;
        std::fstream profilesFile;
        nlohmann::ordered_json profilesJson;
        download::getRequest(JC_COLOR_URL, profilesJson);
        if (profilesJson.empty()) {
            profilesJson = {{{"L_BTN", "0A1E0A"},
                             {"L_JC", "82FF96"},
                             {"R_BTN", "0A1E28"},
                             {"R_JC", "96F5F5"},
                             {"name", "Animal Crossing: New Horizons"}}};
        }
        for (const auto& profiles : {fs::parseJsonFile(path), profilesJson}) {
            for (const auto& x : profiles.items()) {
                std::string name = x.value()["name"];
                brls::Logger::warning(name);
                std::vector<std::string> values = {
                    std::string(x.value()["L_JC"]),
                    std::string(x.value()["L_BTN"]),
                    std::string(x.value()["R_JC"]),
                    std::string(x.value()["R_BTN"])};
                properData = true;
                for (auto& str : values) {
                    if (!isHexaAnd3Bytes(str)) {
                        properData = false;
                    }
                }
                if (properData) {
                    if (name == "") name = "Unamed";
                    auto profile = std::make_pair(name, (std::vector<int>){
                                                            hexToBGR(values[0]),
                                                            hexToBGR(values[1]),
                                                            hexToBGR(values[2]),
                                                            hexToBGR(values[3])});
                    if (name == BACKUP) {
                        res.push_front(profile);
                    }
                    else {
                        res.push_back(profile);
                    }
                }
            }
        }
        return res;
    }

    void changeJCColor(const std::vector<int>& values)
    {
        hiddbgInitialize();
        hidsysInitialize();
        ProgressEvent::instance().setStep(1);
        int res = setColor(values);
        if (res != 0) {
            util::showDialogBoxBlocking("Could not change the Joy-Cons color. Make sure they're docked and try again.\nError :" + std::to_string(res), "Ok");
        }
        hiddbgExit();
        hidsysExit();
        ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
    }

    void backupJCColor(const std::string& path)
    {
        hiddbgInitialize();
        hidsysInitialize();
        ProgressEvent::instance().setStep(1);
        json backup;
        json profiles;
        std::fstream profilesFile;
        if (std::filesystem::exists(path)) {
            profilesFile.open(path, std::fstream::in);
            profilesFile >> profiles;
            profilesFile.close();
        }

        std::vector<int> oldBackups;
        int i = 0;
        for (const auto& x : profiles.items()) {
            if (x.value()["name"] == BACKUP) {
                oldBackups.push_back(i);
            }
            i++;
        }
        for (auto& k : oldBackups) {
            profiles.erase(profiles.begin() + k);
        }

        /* Antes esto era `while (backup.empty())` a secas. backupProfile() lee el
           color del mando en P1, y en una consola que no tiene mando que leer
           —una Lite, por ejemplo— esa lectura falla siempre: el bucle giraba
           para siempre, sin pausa y sin mirar si el usuario pedia cancelar. La
           barra se quedaba en 1%, la B no respondia porque el trabajo nunca
           volvia, y solo se salia reiniciando la consola.

           Ahora se reintenta un rato y se cede el procesador entre intentos. */
        constexpr int INTENTOS = 60;             // unos tres segundos
        constexpr u64 ESPERA_NS = 50'000'000ULL;  // 50 ms

        for (int intento = 0; intento < INTENTOS && backup.empty(); ++intento) {
            if (ProgressEvent::instance().getInterupt())
                break;

            backup = backupProfile();
            if (backup.empty())
                svcSleepThread(ESPERA_NS);
        }

        if (backup.empty()) {
            hiddbgExit();
            hidsysExit();
            ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
            if (!ProgressEvent::instance().getInterupt())
                util::showDialogBoxBlocking("menus/joy_con/no_controller"_i18n, "menus/common/ok"_i18n);
            return;
        }

        profiles.push_back(backup);
        //backup.push_back(profiles);
        fs::writeJsonToFile(profiles, path);
        hiddbgExit();
        hidsysExit();
        ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
    }

}  // namespace JC

namespace PC {

    int setColor(const std::vector<int>& colors)
    {
        Result pads, pc;
        int res = 0;
        s32 nbEntries;
        HidsysUniquePadId UniquePadIds[1] = {};

        pads = hidsysGetUniquePadsFromNpad(HidNpadIdType_No1, UniquePadIds, 1, &nbEntries);
        if (R_SUCCEEDED(pads)) {
            /* Con cuatro colores hay que usar UpdateDesignInfo: UpdateControllerColor
               solo sabe de cuerpo y botones, y los grips se quedarian como estaban.
               Los perfiles guardados siguen trayendo dos, y por ahi se sigue usando
               la de siempre. */
            if (colors.size() >= 4)
                pc = hiddbgUpdateDesignInfo(colors[0], colors[1], colors[2], colors[3], 0, UniquePadIds[0]);
            else
                pc = hiddbgUpdateControllerColor(colors[0], colors[1], UniquePadIds[0]);

            if (R_FAILED(pc)) res += 1;
        }
        else {
            res += 4;
        }
        return res;
    }

    bool readColors(int out[4][3])
    {
        /* Los cuatro colores viven en la SPI del mando, en 0x6050: cuerpo,
           botones, grip izquierdo y grip derecho, tres bytes RGB cada uno.
           hidGetNpadControllerColorSingle solo devuelve los dos primeros. */
        constexpr u32 OFFSET_COLORES = 0x6050;

        s32 nbEntries = 0;
        HidsysUniquePadId UniquePadIds[1] = {};
        if (R_FAILED(hidsysGetUniquePadsFromNpad(HidNpadIdType_No1, UniquePadIds, 1, &nbEntries)) || nbEntries < 1)
            return false;

        u8 crudo[12] = {};
        if (R_FAILED(hiddbgReadSerialFlash(OFFSET_COLORES, crudo, sizeof(crudo), UniquePadIds[0])))
            return false;

        for (int parte = 0; parte < 4; ++parte) {
            out[parte][0] = crudo[parte * 3 + 0];
            out[parte][1] = crudo[parte * 3 + 1];
            out[parte][2] = crudo[parte * 3 + 2];
        }
        return true;
    }

    int backupToJSON(json& profiles, const std::string& path)
    {
        HidNpadControllerColor color;
        Result res = hidGetNpadControllerColorSingle(HidNpadIdType_No1, &color);
        std::vector<int> oldBackups;
        if (R_SUCCEEDED(res)) {
            int i = 0;
            for (const auto& x : profiles.items()) {
                if (x.value()["name"] == BACKUP) {
                    oldBackups.push_back(i);
                }
                i++;
            }
            for (auto& k : oldBackups) {
                profiles.erase(profiles.begin() + k);
            }
            json newBackup = json::object(
                {{"name", BACKUP},
                 {"BODY", BGRToHex(color.main)},
                 {"BTN", BGRToHex(color.sub)}});
            profiles.push_back(newBackup);
            fs::writeJsonToFile(profiles, path);
            return 0;
        }
        else {
            return -1;
        }
    }

    json backupProfile()
    {
        json newBackup;
        HidNpadControllerColor color;
        Result res = hidGetNpadControllerColorSingle(HidNpadIdType_No1, &color);
        if (R_SUCCEEDED(res)) {
            newBackup = json::object(
                {{"name", BACKUP},
                 {"BODY", BGRToHex(color.main)},
                 {"BTN", BGRToHex(color.sub)}});
        }
        return newBackup;
    }

    std::deque<std::pair<std::string, std::vector<int>>> getProfiles(const std::string& path)
    {
        std::deque<std::pair<std::string, std::vector<int>>> res;
        bool properData;
        std::fstream profilesFile;
        nlohmann::ordered_json profilesJson;
        download::getRequest(PC_COLOR_URL, profilesJson);
        if (profilesJson.empty()) {
            profilesJson = {{{"BTN", "e6e6e6"},
                             {"BODY", "2d2d2d"},
                             {"name", "Default black"}}};
        }
        for (const auto& profiles : {fs::parseJsonFile(path), profilesJson}) {
            for (const auto& x : profiles.items()) {
                std::string name = x.value()["name"];
                std::vector<std::string> values = {
                    std::string(x.value()["BODY"]),
                    std::string(x.value()["BTN"])};
                properData = true;
                for (auto& str : values) {
                    if (!isHexaAnd3Bytes(str)) {
                        properData = false;
                    }
                }
                if (properData) {
                    if (name == "") name = "Unamed";
                    auto profile = std::make_pair(name, (std::vector<int>){
                                                            hexToBGR(values[0]),
                                                            hexToBGR(values[1])});
                    if (name == BACKUP) {
                        res.push_front(profile);
                    }
                    else {
                        res.push_back(profile);
                    }
                }
            }
        }
        return res;
    }

    void changePCColor(const std::vector<int>& values)
    {
        hiddbgInitialize();
        hidsysInitialize();
        ProgressEvent::instance().setStep(1);
        int res = setColor(values);
        if (res != 0) {
            util::showDialogBoxBlocking("Could not change the Pro-Con color. Make they're connected to P1. This feature may not work on unofficial controllers. \nError :" + std::to_string(res), "Ok");
        }
        hiddbgExit();
        hidsysExit();
        ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
    }

    void backupPCColor(const std::string& path)
    {
        hiddbgInitialize();
        hidsysInitialize();
        ProgressEvent::instance().setStep(1);
        json backup;
        json profiles;
        std::fstream profilesFile;
        if (std::filesystem::exists(path)) {
            profilesFile.open(path, std::fstream::in);
            profilesFile >> profiles;
            profilesFile.close();
        }

        std::vector<int> oldBackups;
        int i = 0;
        for (const auto& x : profiles.items()) {
            if (x.value()["name"] == BACKUP) {
                oldBackups.push_back(i);
            }
            i++;
        }
        for (auto& k : oldBackups) {
            profiles.erase(profiles.begin() + k);
        }

        /* Antes esto era `while (backup.empty())` a secas. backupProfile() lee el
           color del mando en P1, y en una consola que no tiene mando que leer
           —una Lite, por ejemplo— esa lectura falla siempre: el bucle giraba
           para siempre, sin pausa y sin mirar si el usuario pedia cancelar. La
           barra se quedaba en 1%, la B no respondia porque el trabajo nunca
           volvia, y solo se salia reiniciando la consola.

           Ahora se reintenta un rato y se cede el procesador entre intentos. */
        constexpr int INTENTOS = 60;             // unos tres segundos
        constexpr u64 ESPERA_NS = 50'000'000ULL;  // 50 ms

        for (int intento = 0; intento < INTENTOS && backup.empty(); ++intento) {
            if (ProgressEvent::instance().getInterupt())
                break;

            backup = backupProfile();
            if (backup.empty())
                svcSleepThread(ESPERA_NS);
        }

        if (backup.empty()) {
            hiddbgExit();
            hidsysExit();
            ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
            if (!ProgressEvent::instance().getInterupt())
                util::showDialogBoxBlocking("menus/joy_con/no_controller"_i18n, "menus/common/ok"_i18n);
            return;
        }

        profiles.push_back(backup);
        //backup.push_back(profiles);
        fs::writeJsonToFile(profiles, path);
        hiddbgExit();
        hidsysExit();
        ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
    }

}  // namespace PC