#pragma once

#include <switch.h>

#include <algorithm>
#include <borealis.hpp>
#include <filesystem>
#include <json.hpp>
#include <set>

static constexpr uint32_t MaxTitleCount = 64000;

enum class appPageType
{
    base,
    cheatSlips,
    gbatempCheats
};

class AppPage : public brls::AppletFrame
{
private:
    brls::ListItem* download;
    std::set<std::string> titles;

protected:
    brls::List* list;
    brls::Label* label;
    brls::ListItem* listItem;
    void CreateDownloadAllButton();
    uint64_t GetCurrentApplicationId();
    u32 InitControlData(NsApplicationControlData** controlData);
    uint32_t GetControlData(u64 tid, NsApplicationControlData* controlData, u64& controlSize, std::string& name);
    virtual void PopulatePage();
    virtual void CreateLabel(){};
    virtual void AddListItem(const std::string& name, uint64_t tid);

public:
    AppPage();
};

class AppPage_Exclude : public AppPage
{
private:
    std::set<std::pair<brls::ToggleListItem*, std::string>> items;
    void PopulatePage() override;
    void CreateLabel() override;

public:
    AppPage_Exclude();
};

class AppPage_CheatSlips : public AppPage
{
private:
    void CreateLabel() override;
    void AddListItem(const std::string& name, uint64_t tid) override;

public:
    AppPage_CheatSlips();
};

class AppPage_Gbatemp : public AppPage
{
private:
    void CreateLabel() override;
    void AddListItem(const std::string& name, uint64_t tid) override;

public:
    AppPage_Gbatemp();
};

class AppPage_Gfx : public AppPage
{
private:
    void CreateLabel() override;
    void AddListItem(const std::string& name, uint64_t tid) override;

public:
    AppPage_Gfx();
};

class AppPage_DownloadedCheats : public AppPage
{
private:
    std::set<std::string> titles;
    void CreateLabel() override;
    void AddListItem(const std::string& name, uint64_t tid) override;
    void GetExistingCheatsTids();

public:
    AppPage_DownloadedCheats();
};

class AppPage_OutdatedTitles : public AppPage
{
private:
    nlohmann::ordered_json versions;
    void PopulatePage() override;

    // El escaneo se hace de a poco desde draw(), no de una sola vez en el
    // constructor: pedir el control data de un título es caro y con muchos
    // juegos instalados la app quedaba congelada medio minuto, sin poder
    // cancelar y sin señal de que estuviera haciendo algo.
    std::vector<uint64_t> pending;
    size_t scanned = 0;
    int found = 0;
    bool scanning = false;
    brls::Label* progressLabel = nullptr;

public:
    AppPage_OutdatedTitles();
    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
};