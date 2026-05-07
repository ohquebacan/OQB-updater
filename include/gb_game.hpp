#pragma once

#include <string>
#include <vector>
#include <json.hpp>

class GBCategory {
public:
    GBCategory() {}
    GBCategory(const std::string& name, int id, int index)
        : name(name), id(id), index(index) {}
    std::string getName() const { return name; }
    int getID() const { return id; }
    int getIndex() const { return index; }
private:
    std::string name;
    int id = 0;
    int index = 0;
};

class GBGame {
public:
    GBGame() {}
    GBGame(const std::string& title, const std::string& tid);

    std::string getTitle() const { return title; }
    int getGamebananaID() const { return gamebananaID; }
    std::string getTid() const { return tid; }
    std::vector<GBCategory> getCategories() const { return categories; }
    bool isValid() const { return gamebananaID > 0; }
    bool hasError() const { return gamebananaID == -1; }

private:
    void searchGame();
    void parseJson();
    void loadCategories();

    nlohmann::ordered_json json;
    std::string title;
    std::string tid;
    int gamebananaID = 0;
    std::vector<GBCategory> categories;
};
