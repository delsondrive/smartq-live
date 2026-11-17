#pragma once

#include <functional>
#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// What the SECTION card shows for a section of a song, in its words.
struct SectionCardState {
    bool visible = false;                                         // the song has that section (stored, so it has an id)
    juce::String sectionId, name;
    juce::StringArray types;                                      // the band's section types, then "No type"
    std::vector<juce::Colour> typeColours;                        // their colours (transparent for "No type")
    int type = -1;                                                // into types
    int startBar = 1;
    juce::String length;                                          // "4 bars", "1 bar"
    bool first = false;                                           // it starts the song: it can't move
    bool last = false;                                            // the song's only section: it can't be removed
    bool operator==(const SectionCardState&) const = default;
};

// Not visible unless the song has that section.
SectionCardState makeSectionCardState(const app::Session&, const juce::String& songId, const juce::String& sectionId);

// The right-hand column's SECTION card, while a section is selected: "Name" (typed), "Type" (a Choice of the band's types,
// each with its colour, and "No type"), "Starts at bar" (typed; not for the first section), "Length" and "Remove" (not for
// the only section). Every edit is one section.* command (one undo step), in Edit mode only; a refusal is said in a message.
class SectionCard : public juce::Component {
public:
    explicit SectionCard(app::Session&);

    void show(const juce::String& songId, const juce::String& sectionId);
    const SectionCardState& state() const { return state_; }
    int idealHeight() const;
    std::function<void()> onRemoved;                              // the section went: nothing should be selected

    void setName(const juce::String&);
    void setType(int index);                                      // into state().types; the last is "No type"
    void setStartBar(int bar);
    void remove();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    bool editable() const;
    bool run(const juce::String& command, const juce::var& params);
    void updateControls();
    void finishName(bool keep);
    void finishStart(bool keep);

    app::Session& session_;
    juce::String songId_;
    SectionCardState state_;
    CapsLabel label_{"SECTION"};
    juce::TextEditor name_, start_;
    Choice type_;
    PillButton remove_{"Remove", PillButton::Style::Danger};
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> rowLabels_;
    juce::Rectangle<int> lengthArea_;
    juce::ScopedMessageBox message_;
};

// What the CUE card shows for a song's cue, in its words.
struct CueCardState {
    bool visible = false;                                         // the song has that cue
    juce::String cueId, at;                                       // "Bar 7 · beat 1"
    juce::StringArray says, clips;                                // what each clip says (cueClips), and its path; the cue's own
                                                                  // clip last when it isn't among them
    int clip = -1;                                                // the cue's, into says and clips
    bool operator==(const CueCardState&) const = default;
};

// Not visible unless the song has that cue.
CueCardState makeCueCardState(const app::Session&, const juce::String& songId, const juce::String& cueId);

// The right-hand column's CUE card, while a cue is selected: "At" (its bar and beat; dragged on the lane), "Says" (a Choice
// of the band's clips: cue.set gives the cue that clip and its words, one undo step, in Edit mode only) and "▶ Hear it"
// (Session::hearCue: nothing while a song plays, when the button is disabled; a clip not loaded yet is asked for, and
// heard at the next press).
class CueCard : public juce::Component {
public:
    explicit CueCard(app::Session&);

    void show(const juce::String& songId, const juce::String& cueId);
    const CueCardState& state() const { return state_; }
    int idealHeight() const;

    void setClip(int index);                                      // into state().clips
    bool hear();                                                  // false when nothing was heard

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    app::Session& session_;
    juce::String songId_;
    CueCardState state_;
    CapsLabel label_{"CUE"};
    Choice says_;
    PillButton hear_;
    juce::Rectangle<int> atLabel_, atValue_, saysLabel_;
    juce::ScopedMessageBox message_;
};

// The song's "WHEN A SECTION IS CHOSEN LIVE" card: "It happens at" "The end of the section" or "The next bar"
// (song.setJumpTiming), in Edit mode only.
class JumpTimingCard : public juce::Component {
public:
    explicit JumpTimingCard(app::Session&);

    void show(const juce::String& songId);
    int idealHeight() const;
    void setNextBar(bool);                                        // false: the end of the section

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    app::Session& session_;
    juce::String songId_;
    CapsLabel label_{"WHEN A SECTION IS CHOSEN LIVE"};
    Choice timing_;
    juce::Rectangle<int> rowLabel_;
    juce::ScopedMessageBox message_;
};

}  // namespace sqlive::ui
