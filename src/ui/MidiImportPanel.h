#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "model/MidiFileImport.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// What the right-hand panel of the New song dialog (the mockup's screen 8) says about a MIDI file, in its words.
struct MidiSummary {
    bool present = false;                                         // there is content: the file was read
    juce::String fileName;                                        // "Song.mid", for "FROM “SONG.MID”"
    juce::String tempo, length, sections;                         // "128 BPM · 4/4", "156 bars · 4:52", "7, from the markers"
    std::vector<juce::String> sectionNames;                       // for the pills, in bar order
    struct Track {
        juce::String name;                                        // what its row says before the arrow: "“Gtr 1 FX” (ch 1)"
        juce::String device;                                      // the chosen device's name; "" when it is left out
        bool needsDevice = false;                                 // no device chosen: left out unless one is
        bool operator==(const Track&) const = default;
    };
    std::vector<Track> tracks;
    juce::StringArray notes;                                      // what the reader left out, in plain sentences
    bool operator==(const MidiSummary&) const = default;
};

// Present when there is content; with no choice, defaultMidiChoice's suggestions are shown. A chosen device the band no
// longer has counts as none.
MidiSummary makeMidiSummary(const app::Session&, const juce::File& midiFile, const model::MidiFileContent*,
                            const model::MidiImportChoice*);

// The mockup's .box "FROM “<FILE>”": the rows Tempo, Bar 1 starts at, Length and Sections (with the section names as pills),
// "Pedal changes" with how many tracks, then a row per track, "“<track>” (ch <n>) →" and a Choice of the band's devices
// and "Leave out". A track with no device is in caution with "Choose a device, or it is left out" under it. The file's
// notes follow in dim. With no MIDI file it says "No MIDI file? …"; with one that couldn't be read, why, in danger.
// A view: it reads the session's band for the device names and reports a changed track through onChoiceChanged.
class MidiImportPanel : public juce::Component {
public:
    explicit MidiImportPanel(app::Session&);
    ~MidiImportPanel() override;

    void setContent(const juce::File& midiFile, const model::MidiFileContent*, model::MidiImportChoice);
    void setProblem(const juce::String&);                         // why the file couldn't be read ("" for none)
    void setChoicesEnabled(bool);
    const MidiSummary& summary() const { return summary_; }
    const model::MidiImportChoice& choice() const { return choice_; }

    std::function<void(const model::MidiImportChoice&)> onChoiceChanged;

    int idealHeight(int width) const;                             // everything shown, at that width

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Layout;
    Layout layOut(int width) const;
    void update();                                                // the summary and the track choices again
    void choose(int track, int item);

    app::Session& session_;
    juce::File file_;
    std::optional<model::MidiFileContent> content_;
    model::MidiImportChoice choice_;
    juce::String problem_;
    MidiSummary summary_;
    std::vector<juce::String> deviceIds_;                         // the choices' items, before "Leave out"
    std::vector<std::unique_ptr<Choice>> choices_;
    bool choicesEnabled_ = true;
};

}  // namespace sqlive::ui
