#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/MarkerInspector.h"
#include "ui/QuickActions.h"
#include "ui/SectionInspector.h"
#include "ui/TempoLane.h"
#include "ui/Timeline.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// What the SONG and CLICK cards show (the Song editor's state).
struct SongCardValues {
    double barOneSeconds = 0.0;                                   // "Bar 1 starts at"
    bool clickOn = true, accentFirstBeat = true;
    juce::String clickSound = "wood";                             // as song.setClick takes it: "wood", "beep", "rim", "clicks/<name>.wav"
    int subdivision = 0;                                          // 0..3 None, 8ths, Triplets, 16ths
    bool operator==(const SongCardValues&) const = default;
};

// The CLICK card's Sound choice: "Wood block", "Beep", "Rim", then the band's own sounds by name (bandSounds, as
// CueLibrary::clickSoundsInFolder finds them, keeping those song.setClick takes: clicks/<name>.wav), and the song's own
// even when the folder doesn't have it (yet), then "Import a sound…". sounds[i] is what item i sets ("" for the import).
struct ClickSoundChoice {
    juce::StringArray items, sounds;
    int selected = 0;
    bool operator==(const ClickSoundChoice&) const = default;
};
ClickSoundChoice makeClickSoundChoice(const juce::String& sound, const juce::StringArray& bandSounds);

// The Song editor's right-hand column (the mockup's .insp, 290 px), its cards stacked from the top with 10 px between them:
// the MarkerInspector while a change is selected (scrolling when it is taller than the room it has); else TEMPO while a
// tempo point is selected, SECTION while a section is or CUE while a cue is, then SONG ("Bar 1 starts at", "Set to the
// cursor") and CLICK (On, Sound, Subdivision, Accent the first beat). The Sound's "Import a sound…" asks for a file and
// brings it in with Session::importClickSound, then chooses it. QUICK ACTIONS comes straight under the change's or
// the section's card, when it has something to offer; WHEN A SECTION IS CHOSEN LIVE is always last. The cards that edit a
// selection run their own commands; SONG and CLICK report through the callbacks, and the page runs them.
class EditorInspector : public juce::Component {
public:
    static constexpr int width = 290;

    explicit EditorInspector(app::Session&);

    // The cards for what is picked. False, showing nothing new, when the selection names something the song no longer has
    // (an undo took it): the page then selects nothing.
    bool show(const juce::String& songId, const EditorSelection&, TempoLane*, bool editable);
    void setSongValues(const SongCardValues&, bool editable);
    bool changeShown() const;                                     // the change inspector heads the column
    void forgetTempoLane();                                       // the TEMPO card's lane is about to go

    std::function<void(EditorSelection)> onSelect;                // a change duplicated or deleted, a section removed
    std::function<void(double seconds)> onBarOne;                 // "Bar 1 starts at", typed
    std::function<void()> onBarOneAtCursor;                       // "Set to the cursor"
    std::function<void(bool on, const juce::String& sound, int subdivision, bool accentFirstBeat)> onClick;

    const SectionCard& sectionCard() const { return sectionCard_; }   // for tests
    SectionCard& sectionCard() { return sectionCard_; }
    CueCard& cueCard() { return cueCard_; }
    QuickActions& quickActions() { return quickActions_; }
    const Choice& clickSoundChoice() const { return clickSound_; }   // for tests
    // What "Import a sound…" does with the file chosen: Session::importClickSound, then that sound for the song (onClick); a
    // file that couldn't be brought in is said in a message.
    void importClickSound(const juce::File&);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void finishBarOne(bool keep);
    void sendClick();
    void sendClick(const juce::String& sound);
    void refreshClickSounds(bool lookAtFolder);                  // the Sound choice's items; the folder is read only when asked

    app::Session& session_;
    SongCardValues values_;
    TempoCard tempoCard_;
    MarkerInspector markerInspector_;
    juce::Viewport markerView_;                                   // the change inspector, scrolling when it is taller than its room
    SectionCard sectionCard_;
    CueCard cueCard_;
    QuickActions quickActions_;
    JumpTimingCard jumpCard_;
    CapsLabel songLabel_{"SONG"}, clickLabel_{"CLICK"};
    juce::TextEditor barOneEditor_;
    PillButton setToCursor_{"Set to the cursor"};
    Switch clickOn_, accent_;
    Choice clickSound_, subdivision_;
    ClickSoundChoice soundChoice_;                                // what clickSound_ offers
    juce::StringArray bandSounds_;                                // clicks/ as last read: on show() and after an import
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::ScopedMessageBox message_;

    // Laid out by resized(), drawn by paint().
    juce::Rectangle<int> songCard_, clickCard_;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> rowLabels_;   // the SONG and CLICK cards' row labels
};

}  // namespace sqlive::ui
