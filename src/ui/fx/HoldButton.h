#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace smartq {

// A button that is on only while held down, for a listening button such as Width's Mono or Match: nothing it switches is a
// parameter, and nothing it switches can be left on. It calls onHeld(true) when the left button presses it, and onHeld(false) when
// that is let go, wherever the pointer is then, and also when it is hidden, disabled, taken out of its parent or destroyed while
// held, or any of its parents is hidden or disabled, as a page is when another is shown. Each call is a change: a release after it
// has let go says nothing more. Message thread.
//
// It is drawn in its accent as the mockups have it: a rounded key, lit and pressed in while held.
class HoldButton final : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit HoldButton(juce::String text);
    ~HoldButton() override;

    std::function<void(bool held)> onHeld;

    bool isHeld() const noexcept { return held_; }
    const juce::String& text() const noexcept { return text_; }

    void setAccent(juce::Colour);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void visibilityChanged() override;
    void enablementChanged() override;
    void parentHierarchyChanged() override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    class ParentWatcher;

    // Whether it has a parent, is enabled, and it and every parent below the window at the top are visible: a hold is only kept
    // while that is so.
    bool canBeHeld() const;
    void setHeld(bool);
    void letGoIfHidden();

    const juce::String text_;
    juce::Colour accent_;
    bool held_ = false;
    std::unique_ptr<ParentWatcher> watcher_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HoldButton)
};

}  // namespace smartq
