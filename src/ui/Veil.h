#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace sqlive::ui {

// The mockup's .veil: the app dimmed behind a dialog that sits in the middle, its own width or the veil's less 80, and as
// tall as it says it needs (or the veil's height less 80). It takes every click outside the dialog, and keeps the keys
// pressed in it from reaching the app. Its owner gives it its bounds; or coverAndFollow puts it over a host and keeps it
// there as the host changes size.
class Veil : public juce::Component, private juce::ComponentListener {
public:
    Veil(std::unique_ptr<juce::Component> content, int width, std::function<int()> idealHeight);
    ~Veil() override;

    juce::Component& content() { return *content_; }
    // Added to the host, over all of it, following its size; the dialog takes the keyboard when it can.
    void coverAndFollow(juce::Component& host);

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override { return true; }

private:
    void componentMovedOrResized(juce::Component&, bool wasMoved, bool wasResized) override;

    std::unique_ptr<juce::Component> content_;
    int width_;
    std::function<int()> idealHeight_;
    juce::Component* host_ = nullptr;
};

}  // namespace sqlive::ui
