#pragma once

#include <borealis.hpp>
#include "gb_mod.hpp"

class ModListPage : public brls::AppletFrame {
public:
    ModListPage(const GBGame& game, int page = 1, const std::string& search = "");

private:
    void buildList();

    brls::List* list;
    GBGame game;
    GBModList modList;
    int page;
    std::string search;
};

class ModsTab : public brls::List {
public:
    ModsTab();
};
