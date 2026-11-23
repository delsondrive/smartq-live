#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <initializer_list>
#include <memory>
#include <vector>

#include "ui/fx/params/ModuleControls.h"
#include "ui/fx/NothingArriving.h"
#include "ui/fx/WarningNote.h"

namespace smartq {

class ChainHost;

// A row of a slot's controls, built from its type's table: a knob for a Float, a switch that says On or Off for a Bool and a strip
// for a Choice, each captioned with its host name, with values in its unit (a Width's Balance by its side: "L 26", "Centre"; a ratio as
// "3.0 :1"), in the slot's accent. A "listen" control carries "Nothing routed" and "Nothing arriving" under it, as Dynamic's page does.
// A control may have a note beside it, such as the delay that it adds, and a strip may say more than its labels.
//
// The controls are bound to their parameters, so an edit from anywhere appears on them, and each of them is its own host gesture:
// a knob's drag, which closes at its release or with the knob, or a click.
class ControlRow final : public juce::Component {
public:
    ControlRow(ChainHost&, int slot, std::initializer_list<const char*> fields);
    ~ControlRow() override;

    // The control of one of the fields given, or null.
    juce::Component* controlFor(const char* field) noexcept;

    // The words over it, such as "THRESHOLD", or null.
    juce::Label* captionFor(const char* field) noexcept;

    // Its tooltip.
    void setTooltip(const char* field, const juce::String&);

    // A reading beside one of the fields, which the owner writes and shows. The room for it is kept whether it shows or not.
    juce::Label& addNote(const char* field, const juce::String& tooltip);

    // The words of a Choice's strip, its choices' labels until the owner says more in them, such as a Multiband's "Linear phase, 91 ms".
    // Each segment is then as wide as its words, and the row is laid out again.
    void setChoiceWords(const char* field, const juce::StringArray& words);
    juce::StringArray choiceWords(const char* field);

    // A switch of the page's own after the fields, bound to no parameter, which says On or Off as theirs do and which the owner
    // listens to and sets. Found by `field`, as theirs are, under its caption upper-cased.
    juce::Button& addSwitch(const char* field, const juce::String& caption);

    // A listen control's warnings, hidden while there is nothing to say and in a row without one.
    WarningNote& nothingRouted() noexcept { return nothingRouted_; }
    WarningNote& nothingArriving() noexcept { return nothingArriving_; }

    // One step of the display: the warnings.
    void update(double dtSeconds);

    // There is nothing to close here: a knob's drag closes its gesture at its release or with the knob, and a click is a whole one.
    void endGestures() {}

    // The height it takes at a width, in one row, or two when the width won't hold all of it in one.
    int heightFor(int width) const;
    bool fitsOneRow(int width) const;

    void resized() override;

private:
    struct Control {
        const ControlSpec* spec = nullptr;  // null for a switch of the owner's
        juce::String field;
        ControlKind kind = ControlKind::Bool;
        std::unique_ptr<juce::Component> component;
        std::unique_ptr<juce::Label> caption;
        std::unique_ptr<juce::Label> note;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> switchAttachment;  // after the switch, which it outlives
        juce::StringArray words;  // a Choice's own words, or empty for its labels
    };

    int cellWidth(const Control&) const;
    int stripWidth(const Control&) const;
    int naturalWidth(size_t from, size_t to) const;
    void placeRow(size_t from, size_t to, juce::Rectangle<int> row, int knobSize);
    Control* find(const char* field) noexcept;
    void addCaption(Control&, const juce::String& text);

    ChainHost& processor_;
    const int slot_;
    std::vector<Control> controls_;
    std::atomic<float>* listen_ = nullptr;
    WarningNote nothingRouted_{"Nothing routed"};
    WarningNote nothingArriving_{"Nothing arriving", WarningNote::Look::Quiet};
    NothingArriving arrival_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlRow)
};

}  // namespace smartq
