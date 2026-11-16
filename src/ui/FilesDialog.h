#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// One of a conflict's two files: its name ("song.json", "song-LAPTOP.json") and when it was last changed.
struct FileVersion {
    juce::String fileName, time;
    bool operator==(const FileVersion&) const = default;
};

// What the files dialog shows: the load report's notes, then one row per conflict copy, "<song or document> was changed on
// two computers", with its two versions.
struct FilesState {
    struct Conflict {
        model::ConflictCopy conflict;
        juce::String title;
        FileVersion original, copy;
    };
    juce::StringArray notes;
    std::vector<Conflict> conflicts;

    bool operator==(const FilesState&) const;
    bool isEmpty() const { return notes.isEmpty() && conflicts.empty(); }
};

// From Session::loadReport() and Session::conflictCopies(); looks at the two files' dates, nothing more.
FilesState makeFilesState(const app::Session&);

// The banner's words: "<names> changed on another computer.", the names joined with ", " and a last " and ". "" for none.
juce::String outsideChangesSentence(const std::vector<app::OutsideChange>&);

// The dialog the "Check files" pill opens (Edit mode only), drawn as the mockup's dialogs are: the title "Check files", the
// notes, then each conflict with its two versions side by side, each with its file name, its time and "Keep this one"
// under it, which calls Session::resolveConflict; and "Close". A failure is said in the footer. When nothing is left to
// show, it calls onClose.
class FilesDialog : public juce::Component {
public:
    static constexpr int width = 580;

    explicit FilesDialog(app::Session&);
    ~FilesDialog() override;

    void refresh();                                               // the state again from the session; laid out again if it changed
    const FilesState& state() const { return state_; }
    void keep(const model::ConflictCopy&, bool keepTheCopy);     // a "Keep this one"

    std::function<void()> onClose;                                // "Close", Escape, or nothing left to show

    int idealHeight() const;                                      // everything shown without scrolling, at its width

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    class Body;

    app::Session& session_;
    FilesState state_;
    juce::String error_;

    juce::Viewport view_;
    std::unique_ptr<Body> body_;
    PillButton close_{"Close"};
    juce::Rectangle<int> header_, footer_;
};

// The amber banner over the top of an Edit page while files changed on another computer wait for an answer:
// "<names> changed on another computer." with "Load the changes" (Session::takeOutsideChanges) and "Keep mine"
// (Session::keepMine). Its owner calls refresh() when the session changes, and shows it while wanted().
class OutsideChangesBanner : public juce::Component {
public:
    static constexpr int height = 48;

    explicit OutsideChangesBanner(app::Session&);

    void refresh();
    bool wanted() const { return sentence_.isNotEmpty(); }        // Edit mode, with outside changes waiting
    const juce::String& sentence() const { return sentence_; }
    // "Load the changes" (as its button does): a failure is said in the banner, in place of the sentence, until the next answer.
    void loadChanges();
    const juce::String& failure() const { return failure_; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    app::Session& session_;
    juce::String sentence_, failure_;
    PillButton load_{"Load the changes", PillButton::Style::Primary}, keep_{"Keep mine"};
};

}  // namespace sqlive::ui
