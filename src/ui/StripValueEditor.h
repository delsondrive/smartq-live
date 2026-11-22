#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/ChannelStrip.h"

namespace sqlive::ui {

// The small text field a strip's level or pan is typed into (the mockup's .num.typing): laid over the strip's level box
// or its pan bar, 2 px larger all round for its glow, drawn as the number box (recessed near-black, 4 px corners) with a
// mint outline and a soft mint ring, the words left-aligned 6 px in, in the box's font, with a mint caret.
//
// start() puts it on a strip over that box with the words to begin from, all selected, and gives it the keyboard (when
// it is on screen). Enter (Shift+Enter too) calls onDone(true), Esc onDone(false), and the keyboard going elsewhere (a
// click outside it) onDone(true), each at once and only once per start(): the owner reads text() and calls stop(), which
// takes it off the strip. It exists in the component tree only while typing, so nothing in a mixer wants the keyboard
// otherwise (Show mode's keys reach the window).
class StripValueEditor : public juce::Component {
public:
    StripValueEditor();
    ~StripValueEditor() override;

    void start(ChannelStrip&, ChannelStrip::Field, juce::Rectangle<int> box, const juce::String& text);
    void stop();                                                  // off the strip; onDone isn't called
    bool isOpen() const { return open_; }
    ChannelStrip::Field field() const { return field_; }
    void place(juce::Rectangle<int> box);                         // over the box again, after the strip was laid out

    juce::String text() const;
    void setText(const juce::String&);                            // for tests: what was typed
    bool press(const juce::KeyPress&);                            // for tests: a key typed into it
    void keyboardWentElsewhere();                                 // as when a click elsewhere takes the keyboard

    std::function<void(bool keep)> onDone;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class Field;
    void done(bool keep);

    ChannelStrip::Field field_ = ChannelStrip::Field::Level;
    bool open_ = false;
    std::unique_ptr<Field> field_editor_;                         // last: gone first, while open_ is still there to read
};

}  // namespace sqlive::ui
