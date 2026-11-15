#pragma once

#include <borealis.hpp>

#include "installed_updates.hpp"

// Tab "Updates": installed homebrew / overlays / sysmodules and their newest GitHub release.
class UpdatesTab : public brls::List
{
public:
    UpdatesTab();

private:
    void addSection(const std::vector<updates::Item>& items, updates::Status status, const std::string& title);
};
