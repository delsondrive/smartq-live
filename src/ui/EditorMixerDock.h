#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/MixerPanel.h"
#include "ui/Timeline.h"

namespace sqlive::ui {

// The Song editor's mixer under the tracks (the spec's "Where the mixer appears", Song editor; the mockup's #editor .split
// and .dock): an 18 px splitter bar, then a MixerPanel on "This song" for the song being edited, 8 px in from the top, 10 at
// the sides and 6 at the foot.
//
// The splitter: a dark bar between two hairlines, "MIXER · THIS SONG" at its left, a grip in the middle and "▾ Hide" at its
// right (the mockup's .fold), which folds the dock to the bar alone; folded, it says "▴ Mixer" and unfolds it. Dragging the
// bar up or down makes the panel taller or shorter, within two limits: the tracks above keep the ruler and at least two
// part lanes (minimumTracksHeight), and the panel stays tall enough for a whole strip with a 120 px fader
// (minimumPanelHeight). When a window is too small for both, the strip wins. A panel never sized by hand is 440 px (the
// mockup's on a 1080 screen), or half the room when that is less, but never less than the strip needs. Its height and its
// folded state are this computer's (Settings::editorMixerHeight / editorMixerFolded), written with the session's next
// flush once a drag ends or the fold changes; folding keeps the height, so unfolding brings it back.
//
// Selection is linked with the tracks both ways, with one notification each way and no loop. A strip chosen in the mixer
// (a press, Tab, Esc) selects its part's track (EditorSelection::Kind::Part) and scrolls the tracks to it; Click or Cues
// (which have no track), or no strip at all, clear a part's track selection. A track's selection followed here (followTracks)
// selects its strip and scrolls the mixer to it, through MixerPanel::selectStrip, which tells nothing back; any other
// selection on the timeline leaves no strip selected. When the strips are made again (a part added, removed or moved) the
// panel keeps the same part's strip selected, and the dock keeps the track in step with it. Esc in the mixer gives the
// keys to keysHome (the Song editor, whose tracks take them), never to whatever had them before.
class EditorMixerDock : public juce::Component {
public:
    static constexpr int splitterHeight = 18, defaultPanelHeight = 440;

    EditorMixerDock(app::Session&, Timeline&);
    ~EditorMixerDock() override;

    void setSong(const juce::String& songId);                     // the mixer shows This song for it
    void setKeysHome(juce::Component*);                           // where Esc in the mixer gives the keys
    void followTracks(const EditorSelection&);                    // the timeline's selection changed

    void setFolded(bool);                                         // kept in Settings
    bool folded() const { return folded_; }
    // The dock's whole height (the splitter, and the panel unless folded) in a room of this height (the tracks and the dock
    // together); remembered for the splitter's drags.
    int heightFor(int room);
    int panelHeight() const;                                      // as laid out; 0 folded
    int minimumPanelHeight() const;                               // a whole strip, a 120 px fader
    int maximumPanelHeight() const;                               // in the room last given: the tracks keep two lanes
    static int minimumTracksHeight();                             // the ruler and two part lanes

    std::function<void()> onLayoutChanged;                        // a drag or a fold: the page lays out again
    std::function<void()> onKeysChanged;                          // the mixer took the keyboard or let it go (MixerPanel::hasKeys)

    MixerPanel& mixer() { return mixer_; }
    const MixerPanel& mixer() const { return mixer_; }
    juce::Component& splitter();                                  // for tests: the bar that drags
    juce::Button& foldButton();                                   // for tests: "▾ Hide" / "▴ Mixer"
    juce::String splitterWords() const;                           // "MIXER · THIS SONG"
    juce::String foldWords() const;

    void resized() override;

private:
    class Splitter;
    class FoldButton;

    void stripChosen(int index);
    void stripsMadeAgain();
    int stripOf(const juce::String& partId) const;
    int clampPanel(int height) const;
    void dragged(int startHeight, int dy, bool end);

    app::Session& session_;
    Timeline& timeline_;
    MixerPanel mixer_;
    std::unique_ptr<Splitter> splitter_;
    std::unique_ptr<FoldButton> fold_;
    bool folded_ = false;
    int chosen_ = 0;                                              // the height chosen by hand; 0 none (the default)
    int room_ = 0;                                                // the room last given (heightFor)
    bool fromMixer_ = false;                                      // following the mixer's own choice: the tracks' echo is ignored
};

}  // namespace sqlive::ui
