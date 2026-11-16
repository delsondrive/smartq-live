#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace sqlive::ui {

// Mockup screen 5's amber banner, across the top of the content while another window has the keyboard in Show mode:
// "⌨ The keyboard isn't reaching Smart Q Live" and the sentence after it. A click anywhere on it calls onClick.
class KeyboardBanner : public juce::Component {
public:
    static constexpr int height = 48;                             // 12 px above and below an 18 px line

    KeyboardBanner();
    std::function<void()> onClick;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
};

}  // namespace sqlive::ui
