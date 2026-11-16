#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/BarRuler.h"
#include "ui/BarScale.h"
#include "ui/TapTempo.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

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
    int clickSound = 0, subdivision = 0;                          // 0..2 Wood block, Beep, Rim; 0..3 None, 8ths, Triplets, 16ths
    juce::String saved;  bool saveFailed = false;                 // "Saved ✓", "Saving…", or Session::saveError() (in danger)
    bool operator==(const SongEditorState&) const = default;
};

// An unknown song id gives the empty state: no songId, no parts, the list with nothing selected.
SongEditorState makeSongEditorState(const app::Session&, const juce::String& songId, double cursorSeconds);

// The mockup's screen 9, the frame later stages fill in: no arrangement row, no tempo, section, cue or MIDI lanes, no
// automation. Left (200 px): every song by title, the open one outlined in mint, and "+ New song". Under the top bar: the
// title (click to rename), the chips edited in place (BPM typed or tapped, the time signature, the key and the count-in
// from menus) and "▶ Play" / "■ Stop". The timeline: a 150 px header column and the lanes on a BarScale from the song's
// start (count-in included) to its end: the BARS ruler, one lane per part with its waveform in its role's colour (a click
// on the name: Rename, Role, Remove), and the Click lane's ticks. A white cursor line, moved by a click or drag on the
// ruler or the lanes (snapped to the beat; Alt for no snap), follows the playhead while the song plays. Right (290 px):
// SONG ("Bar 1 starts at") and CLICK. Footer: where the cursor is, the key hints and how the save went. With no song open:
// "Choose a song on the left, or add one." and "+ New song".
//
// Every edit is a command (Session::commands()), and only in Edit mode. Space (Edit mode) plays from the cursor or stops.
// The page refreshes on Session::changes() while it is visible, and when it is shown again.
class SongEditorPage : public juce::Component, private juce::ChangeListener {
public:
    explicit SongEditorPage(app::Session&);
    ~SongEditorPage() override;

    void openSong(const juce::String& songId);                    // also Session::setEditFocus; the cursor goes to the song's start
    juce::String songId() const;
    double cursorSeconds() const;                                 // song time: seconds from bar 1, negative in the count-in
    void setCursorSeconds(double);
    std::function<void()> onNewSong;                              // "+ New song"

    // The gestures the controls make, each one command (one undo step), and nothing in Show mode or with no song open.
    void setBpm(double);                                          // the first tempo point; later points are left alone
    void setSignature(int numerator, int denominator);           // likewise
    void setKey(const juce::String&);                             // "" = no key ("–")
    void setCountIn(int bars);
    void tap(double nowSeconds);                                  // "Tap": sets the BPM, to the nearest whole one, from the third tap on
    void setBarOneSeconds(double);
    void setBarOneAtCursor();                                     // the audio under the cursor becomes bar 1, and the cursor goes there
    void setClick(bool on, int sound, int subdivision, bool accentFirstBeat);
    void togglePlay();                                            // plays this song from the cursor, or stops

    const SongEditorState& state() const { return state_; }
    const juce::Viewport& lanesViewport() const;                  // for tests: the scrolling lanes
    const juce::Viewport& songsViewport() const;                  // for tests: the scrolling list of songs
    juce::String drawnTitle() const;                              // for tests: the title as laid out, shortened with "…" to fit

    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    class SongsBody;
    class LanesBody;
    class CursorLine;
    class EditChip;
    class TitleLabel;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refresh(bool force = false);
    void updateControls();
    void moveCursorLine();
    void refreshScale();

    bool editable() const;                                        // Edit mode, with a song open
    bool run(const juce::String& command, const juce::var& params, const juce::String& coalesceKey = {});
    void tell(const juce::String& message);
    void askText(const juce::String& title, const juce::String& text, const juce::String& button,
                 std::function<void(const juce::String&)> then);
    void setFirstTempoPoint(std::optional<double> bpm, int numerator, int denominator, const juce::String& coalesceKey = {});

    // From the controls.
    void pointerAt(float laneX, juce::ModifierKeys);              // a press or drag on the ruler or a lane
    void renameSong();
    void editBpm();
    void finishBpm(bool keep);
    void finishBarOne(bool keep);
    void showSignatureMenu();
    void showKeyMenu();
    void showCountInMenu();
    void showPartMenu(const juce::String& partId);
    void sendClick();

    app::Session& session_;
    SongEditorState state_;
    juce::String songId_;
    double cursor_ = 0.0;
    std::optional<model::SongDoc> doc_;                           // the open song as last shown
    std::shared_ptr<const app::LoadedSong> loaded_;               // its audio, as last shown
    std::optional<BarScale> scale_;                               // across the lane area
    bool fresh_ = false, shownEditable_ = true;
    TapTempo tapTempo_;
    int tapRun_ = 0;                                              // one undo step for each run of taps

    std::unique_ptr<SongsBody> songs_;
    juce::Viewport songsView_, lanesView_;
    std::unique_ptr<LanesBody> lanes_;
    std::unique_ptr<CursorLine> cursorLine_;
    std::unique_ptr<TitleLabel> title_;
    std::array<std::unique_ptr<EditChip>, 4> chips_;             // BPM, signature, key, count-in
    BarRuler ruler_;
    PillButton tap_, play_, setToCursor_, emptyNew_;
    juce::TextEditor bpmEditor_, barOneEditor_;
    CapsLabel songsLabel_{"ALL SONGS"}, songLabel_{"SONG"}, clickLabel_{"CLICK"};
    Switch clickOn_, accent_;
    Choice clickSound_, subdivision_;

    // Laid out by resized(), drawn by paint().
    juce::Rectangle<int> songsColumn_, bar_, timeline_, barsHead_, footer_, inspector_, songCard_, clickCard_, emptyCard_;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> rowLabels_;   // the cards' row labels
    juce::ScopedMessageBox dialog_;
};

}  // namespace sqlive::ui
