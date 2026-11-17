#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "model/Documents.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// A device's patch list as text, one patch per line: "<program> <name>", or "<msb>:<lsb>:<program> <name>" for a patch
// with a bank (an empty bank number is no bank: "1::5 Lead").
juce::String formatPatchList(const std::vector<model::Patch>&);

// Reads such a list: blank lines are skipped, and the name may follow the number after a space, a tab, a comma, a
// semicolon, "=", "-" or ".". "" on success, with out holding the patches in the list's order; otherwise what is wrong and
// on which line ("Line 2 needs a program number first, like “12 Crunch”."), and out is left as it was.
juce::String parsePatchList(const juce::String& text, std::vector<model::Patch>& out);

// Naming a device's patches (Setup's "24 named" / "Name them"), drawn as the mockup's dialogs are: the title, a text box
// with the current list and the hint "Paste a list, or type one patch per line.", then Cancel and Save. Save reads the
// list (parsePatchList) and runs device.setPatches, one undo step, then calls onClose; a line that can't be read, or a
// refusal, is said in the footer in caution (a typo to fix, nothing broken) and the dialog stays open. In Show mode Save
// is off. Escape closes it.
class PatchListDialog : public juce::Component {
public:
    static constexpr int width = 480;

    PatchListDialog(app::Session&, juce::String deviceId);

    juce::TextEditor& text() { return text_; }                    // for tests
    const juce::String& problem() const { return problem_; }
    void save();

    std::function<void()> onClose;                                // Cancel, Escape, or a list that went in

    int idealHeight() const;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    void say(const juce::String&);

    app::Session& session_;
    const juce::String deviceId_;
    juce::String title_, problem_;
    juce::TextEditor text_;
    PillButton cancel_{"Cancel"}, save_{"Save", PillButton::Style::Primary};

    // Laid out by resized(), drawn by paint().
    juce::Rectangle<int> header_, hint_, footer_, problemLine_;
};

}  // namespace sqlive::ui
