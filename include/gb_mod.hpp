#pragma once

#include <string>
#include <vector>
#include <json.hpp>
#include "gb_game.hpp"

class GBFile {
public:
    GBFile() {}
    GBFile(const std::string& name, int size, const std::string& url,
           const std::string& checkSum, const std::string& modName,
           int date, const std::string& fileID, const GBGame& game);

    void loadFile();

    std::string getName() const { return name; }
    std::string getUrl() const { return url; }
    std::string getPath() const { return path; }
    int getSize() const { return size; }
    int getDate() const { return date; }
    bool getRomfs() const { return romfs; }
    std::string getModName() const { return modName; }
    GBGame getGame() const { return game; }

private:
    bool findRomfsRecursive(const nlohmann::ordered_json& obj);

    std::string name;
    int size = 0;
    std::string url;
    std::string checkSum;
    int date = 0;
    std::string path;
    std::string modName;
    std::string fileID;
    GBGame game;
    bool romfs = false;
};

class GBMod {
public:
    GBMod() {}
    GBMod(const std::string& name, int id, const std::string& author, const GBGame& game);

    void loadMod();

    std::string getName() const { return name; }
    int getID() const { return id; }
    std::string getDescription() const { return description; }
    std::string getAuthor() const { return author; }
    std::vector<GBFile>& getFiles() { return files; }

private:
    std::string name;
    int id = 0;
    std::string description;
    std::string author;
    std::vector<GBFile> files;
    GBGame game;
};

class GBModList {
public:
    GBModList() {}
    GBModList(const GBGame& game, int startPage = 1, const std::string& search = "");

    std::vector<GBMod>& getMods() { return mods; }
    int getCurrentPage() const { return currentPage; }
    bool isEmpty() const { return mods.empty(); }

private:
    void updatePage();

    std::vector<GBMod> mods;
    int currentPage = 1;
    std::string currentSearch;
    GBGame game;
    GBCategory currentCategory;
};
