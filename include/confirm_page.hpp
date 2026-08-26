#pragma once

#include <borealis.hpp>
#include <chrono>
#include <memory>
#include <string>

class ConfirmPage : public brls::View
{
protected:
    brls::Button* button = nullptr;
    brls::Label* label = nullptr;
    std::chrono::system_clock::time_point start = std::chrono::high_resolution_clock::now();
    bool done = false;

public:
    ConfirmPage(brls::StagedAppletFrame* frame, const std::string& text);
    ~ConfirmPage();

    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
    void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override;
    brls::View* getDefaultFocus() override;
};

class ConfirmPage_Done : public ConfirmPage
{
public:
    ConfirmPage_Done(brls::StagedAppletFrame* frame, const std::string& text);
};

class ConfirmPage_AppUpdate : public ConfirmPage_Done
{
public:
    ConfirmPage_AppUpdate(brls::StagedAppletFrame* frame, const std::string& text);
};

class ConfirmPage_AmsUpdate : public ConfirmPage_Done
{
public:
    ConfirmPage_AmsUpdate(brls::StagedAppletFrame* frame, const std::string& text, bool erista = true);
};

class ConfirmPage_SelfUpdate : public ConfirmPage_Done
{
public:
    ConfirmPage_SelfUpdate(brls::StagedAppletFrame* frame, const std::string& text);
};

// Página cuyo texto se decide recién al dibujarse. La usa el flujo de
// forwarders: el worker corre en otro hilo y deja ahí su resultado, y el texto
// se lee desde el hilo de la UI, que es el único que puede tocar las vistas.
class ConfirmPage_Deferred : public ConfirmPage_Done
{
private:
    std::shared_ptr<std::string> message;
    bool applied = false;

public:
    ConfirmPage_Deferred(brls::StagedAppletFrame* frame, std::shared_ptr<std::string> message);
    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
};