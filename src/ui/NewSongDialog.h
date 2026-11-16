#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "model/SongImport.h"
#include "ui/MidiImportPanel.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// What the New song dialog shows (the mockup's screen 8), in its words.
struct NewSongState {
    struct Row {
        juce::String file, role, heardIn, mark;
        // role: the role's name, "Choose a role…" or "Leave out". heardIn: "PA and 5 in-ears", "2 in-ears", "nowhere yet",
        // "The app makes its own click" (a click file), the file's problem, or "—". mark "✓", "?", "–".
        bool needsRole = false, leftOut = false, hasProblem = false;
        bool operator==(const Row&) const = default;
    };
    juce::String title;  std::vector<Row> rows;
    juce::String rig;                                             // the current rig's name, for "HEARD IN (TOUR RIG)"
    juce::String setlist;                                         // the current setlist's name, for the tick box; "" when none
    juce::String waiting;                                         // "1 file needs a role", "Nothing to import yet", "" when ready
    juce::String tooBig;                                          // "This song needs 2.4 GB of memory. …", "" when it fits
    bool canCreate = false;                                       // the MIDI file never holds it up
    MidiSummary midi;                                             // the right-hand panel: what the plan's MIDI file holds
    bool operator==(const NewSongState&) const = default;
};

// midi: the plan's MIDI file as read (nullptr when there is none, or it couldn't be read); choice: what is to come in from
// it (nullptr: defaultMidiChoice's suggestions).
NewSongState makeNewSongState(const model::ImportPlan&, const app::Session&, const model::MidiFileContent* midi = nullptr,
                              const model::MidiImportChoice* choice = nullptr);

// The mockup's screen 8: the title, a drop strip that adds files, a table of the files with a preview, the role each plays
// (a choice), where it will be heard in the current rig and a mark; on the right the MidiImportPanel for the plan's MIDI
// file; and a footer with "Add this song to “<setlist>”", what is still to answer, Cancel and Create song.
//
// A view: it never touches the model or the BandFolder. It reads the MIDI file when files are added (once per file) and
// keeps what it holds and the choice made for it, which starts from defaultMidiChoice. Creating runs in three steps:
// Session::reserveImportFolder on the message thread; model::convertSongFiles on the dialog's own thread, on copies, with a
// progress bar and Cancel; then Session::commitImport with the MIDI choice on the message thread (which reads the MIDI file
// again there and brings it in, in the same undo step as the song), and "setlist.addSong" when the tick box is on. A
// reserved folder the conversion never started on is released when the dialog is cancelled or closed, so no empty song
// folder is left behind.
class NewSongDialog : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::ChangeListener,
                      private juce::AsyncUpdater {
public:
    NewSongDialog(app::Session&, juce::Array<juce::File> dropped);
    ~NewSongDialog() override;                                    // stops and joins the conversion, and releases what it reserved

    const model::ImportPlan& plan() const { return plan_; }
    const NewSongState& state() const { return state_; }
    void setRole(int row, const juce::String& roleId);           // also keeps a file that was left out
    void setLeftOut(int row, bool);
    void setTitle(const juce::String&);
    void setTrackDevice(int track, const juce::String& deviceId); // "" leaves the MIDI file's track out
    const model::MidiImportChoice& midiChoice() const { return midiChoice_; }
    void addFiles(const juce::Array<juce::File>&);               // more files, or a folder of them, into the plan
    void create();                                                // converts on a background thread with a progress bar, then commits
    void cancel();                                                // stops a conversion, releases its folder, then onCancelled
    bool importing() const { return worker_ != nullptr; }

    std::function<void(juce::String songId)> onCreated;
    std::function<void()> onCancelled;

    int idealHeight() const;                                      // everything shown without scrolling the table

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    class RowsBody;
    class Converter;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void handleAsyncUpdate() override;
    void refresh(bool force = true);                              // not forced: only when what it shows has changed
    void chooseRole(int row, int item);                           // an item of the row's role choice was picked
    void finish();                                                // the conversion ended: commit it, or say why it couldn't
    void stopConverting();                                        // joins the thread and lets go of what it was converting into
    void browse();
    int footerHeight() const;

    app::Session& session_;
    juce::Array<juce::File> dropped_;                             // everything dropped so far, as dropped
    model::ImportPlan plan_;
    NewSongState state_;
    std::map<juce::String, std::vector<float>> previews_;         // by full path: peaks 0..1 across the file
    juce::String problem_;                                        // why the last create failed, in danger
    bool addToSetlist_ = true, filesOver_ = false;
    double shownProgress_ = 0.0;
    int laidOutHeight_ = 0;                                       // idealHeight() when the parent last laid it out

    // The plan's MIDI file as read when it was added, why it couldn't be, and what is to come in from it.
    juce::File midiFile_;
    std::optional<model::MidiFileContent> midi_;
    juce::String midiProblem_;
    model::MidiImportChoice midiChoice_;

    // The conversion: the folder reserved for it, the thread, and what the thread hands back.
    juce::File reserved_;
    std::unique_ptr<Converter> worker_;
    std::atomic<double> progress_{0.0};
    std::atomic<bool> began_{false}, done_{false};

    juce::TextEditor title_;
    juce::Viewport rowsView_;
    std::unique_ptr<RowsBody> rows_;
    juce::Viewport midiView_;
    MidiImportPanel midiPanel_;
    PillButton cancel_{"Cancel"}, create_{"Create song", PillButton::Style::Primary};
    std::unique_ptr<juce::FileChooser> chooser_;

    // Laid out by resized(), drawn by paint().
    juce::Rectangle<int> header_, steps_, drop_, tableBox_, footer_, tick_, waiting_, progress_bar_, tooBig_;
};

}  // namespace sqlive::ui
