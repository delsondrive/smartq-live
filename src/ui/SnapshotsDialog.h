#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "model/SnapshotChanges.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// What the snapshots dialog lists, newest first: each one's name, whether it was made automatically, and when, in words.
struct SnapshotsState {
    struct Row {
        juce::String id, name, when;
        bool automatic = false;
        bool operator==(const Row&) const = default;
    };
    std::vector<Row> rows;
    bool operator==(const SnapshotsState&) const = default;
};

// When a snapshot was made, as the list says it: "Today 16:42", "Thu 22:05" in the six days before today, otherwise the
// date, "12 Nov 2026, 16:42".
juce::String snapshotWhen(juce::Time when, juce::Time now);

// From Session::snapshots(); reads the snapshots folder.
SnapshotsState makeSnapshotsState(const app::Session&, juce::Time now);

// The mockup's screen 15, opened from the top bar's "Snapshots" (Edit mode only; it closes when Show mode starts): the title
// "Snapshots" and "+ Save a snapshot now" (asks for a name); the list on the left (name, "· automatic" in dim, when), the
// selected one highlighted; on the right, what going back does; the footer's sentence, "Close" and "Go back to this snapshot"
// (ember), which runs SnapshotStore::restore, one undoable step, and closes. Right-click a named snapshot: Rename, Remove.
// A refusal is said in the footer, in place of its sentence. Escape closes it.
class SnapshotsDialog : public juce::Component {
public:
    static constexpr int width = 860;

    explicit SnapshotsDialog(app::Session&);
    ~SnapshotsDialog() override;

    void refresh();                                               // the list again from the snapshots folder
    const SnapshotsState& state() const { return state_; }
    const juce::String& selected() const { return selected_; }   // the selected snapshot's id; "" when there is none
    void select(const juce::String& id);
    const juce::String& problem() const { return problem_; }
    // What differs between the selected snapshot and the band now (at most kMaxChanges lines and a last "and n more changes").
    const std::vector<model::ChangeLine>& changes() const { return changes_; }
    static constexpr int kMaxChanges = 12;

    // What the buttons and the menu do, once a name is given.
    void saveNow(const juce::String& name);                       // a named snapshot of how things are now, then selected
    void goBack();                                                // to the selected one; closes when it went back
    void rename(const juce::String& id, const juce::String& name);
    void remove(const juce::String& id);

    std::function<void()> onClose;                                // "Close", Escape, or after going back

    int idealHeight() const;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    class List;

    bool editable() const;                                        // Edit mode: nothing here runs in a show
    void say(const juce::String&);
    void askName();
    void showMenu(const juce::String& id);
    int bodyHeight() const;
    int changesHeight() const;                                    // the right side's list of changes, with its heading
    void readChanges();                                           // changes_ for selected_

    app::Session& session_;
    SnapshotsState state_;
    juce::String selected_, problem_;
    std::vector<model::ChangeLine> changes_;
    bool changesRead_ = false;                                    // false: the selected snapshot could not be read (or none is)

    juce::Viewport view_;
    std::unique_ptr<List> list_;
    PillButton save_{"+ Save a snapshot now", PillButton::Style::Primary};
    PillButton close_{"Close"}, goBack_{"Go back to this snapshot", PillButton::Style::Ember};
    juce::Rectangle<int> header_, footer_, left_, right_, columns_, hint_;
    juce::ScopedMessageBox box_;
};

}  // namespace sqlive::ui
