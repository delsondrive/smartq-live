#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/ArrangementRow.h"
#include "ui/EditorInspector.h"
#include "ui/TapTempo.h"
#include "ui/TempoLane.h"
#include "ui/Timeline.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

class PartLane;
class Veil;

// What the Song editor shows (the mockup's screen 9, as far as stage 1 goes), in its words.
struct SongEditorState {
    juce::String songId, title;  juce::StringArray chips;         // {"128 BPM", "4/4", "Key E", "Count-in 1 bar"}
    struct PartRow {
        juce::String partId, name, role;  juce::Colour colour;  bool missing = false;   // missing: app::partFileProblems, not in the cloud
        bool operator==(const PartRow&) const = default;
    };
    std::vector<PartRow> parts;
    juce::String footerBar, footerTime;                           // "33.1", "2:14": where the cursor is
    juce::StringArray songs;  int selectedSong = -1;              // the list on the left, by title
    juce::StringArray songIds;                                    // the same songs' ids, in the same order
    bool loading = false;  double progress = 0.0;                 // the song's audio is still loading (SongCache)
    bool playing = false;                                         // this song plays, from the editor
    bool anyPlaying = false;                                      // a song plays (this one or another): Play would stop it
    double barOneSeconds = 0.0;                                   // "Bar 1 starts at": where bar 1 falls in the audio
    bool clickOn = true, accentFirstBeat = true;                  // the CLICK panel
    juce::String clickSound = "wood";                             // as song.setClick takes it: "wood", "beep", "rim", "clicks/<name>.wav"
    int subdivision = 0;                                          // 0..3 None, 8ths, Triplets, 16ths
    juce::String saved;  bool saveFailed = false;                 // "Saved ✓", "Saving…", or Session::saveError() (in danger)
    bool operator==(const SongEditorState&) const = default;
};

// The song's parts whose audio file is missing (app::partFileProblems, which looks at the disk): by part id.
std::set<juce::String> missingParts(const app::Session&, const juce::String& songId);

// An unknown song id gives the empty state: no songId, no parts, the list with nothing selected. The parts' missing files are
// looked for on disk unless missing is given (what missingParts said, kept while the song and its audio stay the same).
SongEditorState makeSongEditorState(const app::Session&, const juce::String& songId, double cursorSeconds,
                                    const std::set<juce::String>* missing = nullptr);

// The mockup's screen 9. Left (200 px):
// every song by title, the open one outlined in mint, and "+ New song". Under the top bar: the title (a click: Rename…,
// Import a MIDI file…), the chips edited in place (BPM typed or tapped, the time signature, the key and the count-in from
// menus), the tools "Select · Draw · Automation" and the snap "Bar · Beat · 1/8", and "▶ Play" / "■ Stop" (a right-click:
// "▶ Play from the cursor", "▶ Play as arranged") (the tools go to a second row when the window is too narrow for one).
// Under that, the ArrangementRow. The Timeline: the Tempo lane, the SectionsLane, the CuesLane, a MidiLane per device
// (midiLaneDevices: the band's, then any the song still has changes for), one PartLane per part (a click on its name:
// Rename, Role, Remove) and the ClickLane, the parts and the click each with its AutomationOverlay, under the BARS ruler,
// with the white cursor line (moved by a click or drag on the ruler or a lane's empty space, snapped as the snap says; Alt
// for no snap), which follows the playhead while the song plays. Right (290 px): the EditorInspector (the change
// inspector, or TEMPO, SECTION or CUE, then SONG and CLICK; QUICK ACTIONS; and WHEN A SECTION IS CHOSEN LIVE).
// Footer: where the cursor is, the key hints and how the save went. With no song open: "Choose a song on the left, or add
// one." and "+ New song".
//
// Every edit is a command (Session::commands()), and only in Edit mode. Space (Edit mode) plays from the cursor or stops;
// Delete removes what is selected.
// The page refreshes on Session::changes() while it is visible, and when it is shown again.
class SongEditorPage : public juce::Component, public juce::DragAndDropContainer, private juce::ChangeListener {
public:
    explicit SongEditorPage(app::Session&);
    ~SongEditorPage() override;

    void openSong(const juce::String& songId);                    // also Session::setEditFocus; the cursor goes to the song's start
    juce::String songId() const;
    double cursorSeconds() const;                                 // song time: seconds from bar 1, negative in the count-in
    void setCursorSeconds(double);
    std::function<void()> onNewSong;                              // "+ New song"
    void importMidiFile(const juce::File&);                       // the title menu's "Import a MIDI file…", once a file is chosen:
                                                                  // the MidiImportDialog for it over the app, on a Veil

    // The gestures the controls make, each one command (one undo step), and nothing in Show mode or with no song open.
    void setBpm(double);                                          // the first tempo point; later points are left alone
    void setSignature(int numerator, int denominator);           // likewise
    void setKey(const juce::String&);                             // "" = no key ("–")
    void setCountIn(int bars);
    void tap(double nowSeconds);                                  // "Tap": sets the BPM, to the nearest whole one, from the third tap on
    void setBarOneSeconds(double);
    void setBarOneAtCursor();                                     // the audio under the cursor becomes bar 1, and the cursor goes there
    void setClick(bool on, const juce::String& sound, int subdivision, bool accentFirstBeat);
    void togglePlay();                                            // plays this song from the cursor, or stops
    void playAsArranged();                                        // from the start, as the arrangement row shows it

    const SongEditorState& state() const { return state_; }
    ArrangementRow& arrangementRow() { return arrangementRow_; }  // for tests and the screenshots
    EditorInspector& inspector() { return inspector_; }           // for tests: the right-hand column
    const juce::Viewport& lanesViewport() const;                  // for tests: the timeline's scrolling lanes
    Timeline& timeline() { return timeline_; }                    // for tests
    const juce::Viewport& songsViewport() const;                  // for tests: the scrolling list of songs
    juce::String drawnTitle() const;                              // for tests: the title as laid out, shortened with "…" to fit

    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    class SongsBody;
    class EditChip;
    class TitleLabel;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refresh(bool force = false);
    void updateControls();
    void buildLanes();                                            // the open song's lanes, when it or its parts changed
    void showInspector();                                         // the cards, or the change inspector, for the selection

    bool editable() const;                                        // Edit mode, with a song open
    bool run(const juce::String& command, const juce::var& params, const juce::String& coalesceKey = {});
    void setFirstTempoPoint(std::optional<double> bpm, int numerator, int denominator, const juce::String& coalesceKey = {});

    // From the controls.
    void showTitleMenu();                                         // "Rename…" and "Import a MIDI file…"
    void renameSong();
    void chooseMidiFile();
    void editBpm();
    void finishBpm(bool keep);
    void showSignatureMenu();
    void showKeyMenu();
    void showCountInMenu();
    void showPlayMenu();                                          // "▶ Play from the cursor" and "▶ Play as arranged"

    app::Session& session_;
    SongEditorState state_;
    juce::String songId_;
    std::optional<model::SongDoc> doc_;                           // the open song as last shown
    std::shared_ptr<const app::LoadedSong> loaded_;               // its audio, as last shown
    std::set<juce::String> missing_;                              // missingParts for doc_ and loaded_, so a refresh
                                                                  // looks at the disk only when either changes
    bool missingKnown_ = false;
    bool fresh_ = false, shownEditable_ = true;
    TapTempo tapTempo_;
    int tapRun_ = 0;                                              // one undo step for each run of taps

    std::unique_ptr<SongsBody> songs_;
    juce::Viewport songsView_;
    Timeline timeline_;
    juce::String laneSong_;                                       // the song and parts the lanes were built for
    std::vector<juce::String> laneParts_, laneDevices_;
    TempoLane* tempoLane_ = nullptr;                              // the timeline's, while it has them
    std::vector<PartLane*> partLanes_;
    EditorInspector inspector_;                                   // the right-hand column
    ArrangementRow arrangementRow_;
    std::vector<model::SectionTypeDoc> typesShown_;               // the band's section types, as last shown
    model::CueLead cueLeadShown_ = model::CueLead::OneBar;        // and how early its cues come
    std::vector<model::MidiDeviceDoc> devicesShown_;              // the band's devices, and their ports' states, as last shown
    std::vector<int> portsShown_;
    std::unique_ptr<TitleLabel> title_;
    std::array<std::unique_ptr<EditChip>, 4> chips_;             // BPM, signature, key, count-in
    PillButton tap_, play_, emptyNew_;                            // a right-click on ▶ Play: showPlayMenu
    juce::TextEditor bpmEditor_;
    CapsLabel songsLabel_{"ALL SONGS"};
    Segmented tools_, snapTo_;                                    // "Select · Draw", "Bar · Beat · 1/8"

    // Laid out by resized(), drawn by paint().
    juce::Rectangle<int> songsColumn_, bar_, footer_, emptyCard_;
    juce::ScopedMessageBox dialog_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<Veil> midiImport_;                            // the MIDI import dialog while it is open
};

}  // namespace sqlive::ui
