#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// What the Setlists page shows (the mockup's screen 7), in its words.
struct SetlistsState {
    struct ListItem {
        juce::String id, name, detail;  bool selected = false;    // detail "7 songs · 27:45", "No songs yet"
        bool operator==(const ListItem&) const = default;
    };
    struct Row {
        juce::String entryId, songId;  int number = 0;
        juce::String title, sub, length, then, warning;
        // sub "128 BPM · E · count-in 1 bar" ("no count-in"). then: what the THEN column shows. Song by song: "Stop" in
        // every row. Continuous: "Stop", "Gap 4 s" or "Seamless". The last row: "end of the set" in both modes.
        // warning: "1 file missing", "3 files still in the cloud", or "".
        bool operator==(const Row&) const = default;
    };
    struct LibraryItem {
        juce::String songId, title, note;  bool inThisSet = false;   // note "4:02" or "in this set"
        bool operator==(const LibraryItem&) const = default;
    };
    std::vector<ListItem> lists;                                  // by name
    juce::String title, summary;                                  // the chosen setlist's name; "7 songs · 27:45" or "No songs yet"
    bool continuous = false;
    std::vector<Row> rows;
    std::vector<LibraryItem> library;                             // every song in the band whose title has the search in it, by title
    juce::String search;
    int songCount = 0;                                            // every song in the band, whatever the search: "ALL SONGS · 23"
    bool operator==(const SetlistsState&) const = default;
};

SetlistsState makeSetlistsState(const app::Session&, const juce::String& search);

// The mockup's screen 7. Left (200 px): the band's setlists; a click chooses one, a right-click renames or removes it,
// and "+ New setlist" makes one. Middle: the chosen setlist's title and summary, "After each song" (continuous or song by
// song), and its running order: rows dragged by their grip to reorder, removed with Delete or a right-click, a THEN
// choice of what follows each song (dimmed, and "Stop", song by song), and "end of the set" on the last. Right (240 px):
// every song in the band, searchable, dragged into the running order (or double-clicked to add it at the end), removed
// from the band with a right-click (asked first), and "+ New song". Files dropped anywhere on the page ask for a new song.
//
// Every edit is a command (Session::commands()), and only in Edit mode. The page refreshes on Session::changes() while
// it is visible, and when it is shown again.
class SetlistsPage : public juce::Component,
                     public juce::DragAndDropContainer,
                     public juce::FileDragAndDropTarget,
                     private juce::ChangeListener {
public:
    explicit SetlistsPage(app::Session&);
    ~SetlistsPage() override;

    std::function<void(const juce::String& songId)> onOpenSong;   // double-click a row or a library song: open it in the Song editor
    std::function<void(juce::Array<juce::File>)> onNewSong;       // "+ New song" (no files) or files dropped on the page

    const SetlistsState& state() const { return state_; }
    const juce::Viewport& tableViewport() const;                  // for tests: the scrolling table of rows
    const juce::Viewport& listsViewport() const;                  // for tests: the scrolling list of setlists
    juce::String drawnTitle(int row) const;                       // for tests: the row's title as laid out, shortened with "…" to fit
    juce::String drawnHeading() const;                            // for tests: the setlist's name as laid out above the table

    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

private:
    class ListsBody;
    class TableBody;
    class LibraryBody;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refresh(bool force = false);

    bool editable() const;                                        // Edit mode
    bool run(const juce::String& command, const juce::var& params);   // says why when it is refused
    void tell(const juce::String& message);
    // A question with a line of text to type: then(text) when it is answered; when it is cancelled, what the page shows
    // goes back to what the band says (a THEN choice that asked).
    void askText(const juce::String& title, const juce::String& message, const juce::String& text, const juce::String& button,
                 std::function<void(const juce::String&)> then);

    // Gestures, from the page's parts.
    void newSetlist();
    void renameSetlist(const juce::String& setlistId);
    void removeSetlist(const juce::String& setlistId);
    void setContinuous(bool);
    void chooseThen(const juce::String& entryId, const juce::String& item);
    void askGap(const juce::String& entryId);
    void addSong(const juce::String& songId, int index);          // index -1: at the end
    void moveEntry(const juce::String& entryId, int toIndex);
    void removeEntry(const juce::String& entryId);
    void askRemoveSong(const juce::String& songId);
    void openSong(const juce::String& songId);

    app::Session& session_;
    SetlistsState state_;
    bool fresh_ = false;                                          // state_ is up to date with the session
    bool shownEditable_ = true;                                   // the controls were last set for Edit mode
    bool filesOver_ = false;                                      // files from the system are dragged over the page

    CapsLabel listsLabel_{"SETLISTS"}, libraryLabel_;
    Segmented playMode_;
    juce::TextEditor search_;
    PillButton newSong_;
    juce::Viewport listsView_, tableView_, libraryView_;
    std::unique_ptr<ListsBody> lists_;
    std::unique_ptr<TableBody> table_;
    std::unique_ptr<LibraryBody> library_;
    juce::String heading_;                                        // the title as laid out
    juce::Rectangle<int> headingArea_, tableBox_, libraryBox_;
    juce::ScopedMessageBox dialog_;
};

}  // namespace sqlive::ui
