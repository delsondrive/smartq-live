#pragma once

#include <functional>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "model/MidiFileImport.h"
#include "ui/MidiImportPanel.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// Bringing a MIDI file into a song that is already there (the Song editor's "Import a MIDI file…"), drawn as the mockup's
// dialogs are: the title "Import a MIDI file", the MidiImportPanel for the file, and a footer with three tick boxes, Tempo,
// Sections and Pedal changes (off: no lane is touched), Cancel and Import. Import runs "song.importMidi" for the song, one
// undo step, then calls onClose; a refusal is said in the footer, in danger. A file that can't be read shows why in place
// of what it holds, and Import is off. A view: it reads the file once, here, and changes the band only through the command.
class MidiImportDialog : public juce::Component {
public:
    static constexpr int width = 480;

    MidiImportDialog(app::Session&, juce::String songId, juce::File midiFile);

    const juce::String& problem() const { return problem_; }     // why the file couldn't be read, "" when it was
    const model::MidiImportChoice& choice() const { return choice_; }
    void setTicks(bool tempo, bool sections, bool pedalChanges);
    void import();                                                // nothing in Show mode, or when the file couldn't be read

    std::function<void()> onClose;                                // Cancel, Escape, or an import that went in

    int idealHeight() const;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    void update();

    app::Session& session_;
    const juce::String songId_;
    const juce::File file_;
    std::optional<model::MidiFileContent> content_;
    model::MidiImportChoice choice_;                              // the panel's: tempo and sections come from the ticks
    bool tempo_ = true, sections_ = true, pedalChanges_ = true;
    juce::String problem_, refused_;

    juce::Viewport view_;
    MidiImportPanel panel_;
    PillButton cancel_{"Cancel"}, import_{"Import", PillButton::Style::Primary};

    // Laid out by resized(), drawn by paint().
    juce::Rectangle<int> header_, footer_, refusedLine_;
    juce::Rectangle<int> ticks_[3];
};

}  // namespace sqlive::ui
