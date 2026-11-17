#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/CueVoicePage.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

// What the cue voice page and its rows share. The mockup's CSS: td (padding 7 8, 13 px in mid), td.f (text, semibold), a
// 36 px row holding a 24 px .dd, .pills span (11 px bold on the colour, 4 px corners, padding 1 6), .hint (12 px dim).
namespace cuevoice {
inline constexpr int kCellPadX = 8, kRowHeight = 36, kChoiceHeight = 24, kPlayWidth = 36, kPillPadX = 6, kPillHeight = 16;
inline constexpr int kHintGap = 8;
inline const juce::String kPlay = utf8("\xe2\x96\xb6");          // ▶
inline juce::Font bodyFont() { return theme::font(theme::sizeBody); }
inline juce::Font nameFont() { return theme::font(theme::sizeBody, true); }
inline juce::Font hintFont() { return theme::font(theme::sizeSmall); }
inline juce::Font pillFont() { return theme::font(theme::sizeCaption, true); }
}  // namespace cuevoice

// The cue voice table's rows, which the page's viewport scrolls when there are many: each type's name ("· yours" after one
// the band added; a click renames it), its colour pill (a click: the colour menu), its CUE choice (a child; makeCueChoice)
// with why a missing clip is in caution after it, and ▶ to hear it. The columns' widths are the page's.
class CueVoicePage::Rows : public juce::Component {
public:
    explicit Rows(CueVoicePage& page) : page_(page) {}

    void update();                                                // the choices, from the page's state
    void layout();
    const Choice* choice(int row) const;
    // After a missing clip: "not made yet" for a built-in one, "file missing" for a recording (the app can't make that).
    static juce::String hintOf(const CueVoiceState::Row&);
    static int hintWidth(const CueVoiceState::Row&);

    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> pillArea(int row) const;
    juce::Rectangle<int> playArea(int row) const;

    CueVoicePage& page_;
    std::vector<std::unique_ptr<Choice>> choices_;
    std::vector<CueChoice> shown_;
    int hovered_ = -1;
};

}  // namespace sqlive::ui
