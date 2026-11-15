#pragma once

#include <borealis.hpp>

#include "user_links.hpp"

// Tab "Eigene Links": the user adds GitHub repos / direct links, says what they are
// (app, theme, cheats, mod, other, CFW pack), where to save and whether to unpack.
class UserLinksTab : public brls::List
{
public:
    UserLinksTab();

    static void startInstall(const userlinks::Link& link);

private:
    void addSection(const std::vector<userlinks::Link>& links, userlinks::Kind kind);

    static void openEditor(int index);
    static void askRemove(const userlinks::Link& link);
};
