#pragma once

#include <functional>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/MeterReader.h"
#include "ui/MixerState.h"

namespace sqlive::ui {

// One strip of the mixer console (the spec's "The strip"; the mockup's .strip, drawn by StripParts), top to bottom:
//  - a 4 px bar along the top in the strip's colour;
//  - the pan bar (96 px; 74 px beside the link button on an output view): "C", "L 40", "R 100", a faint fill from the centre
//    to the value and a marker at it; "Follow" in a dashed outline while an output's role is linked. Left empty (the row
//    kept) for a strip without a pan (Click and Cues on "This song");
//  - M (danger red when on; a hollow red outline when another part's solo silences this one) and S (caution amber when on;
//    only where the state allows a solo, never compact). A strip that can't be muted keeps M's place empty;
//  - two number boxes, never compact: the level ("−3.0", "+2.0", "off"; caution when changed tonight) and the peak (the
//    loudest moment since it was reset, "−∞" before anything; danger red above 0 dBFS). A click on the peak box resets it;
//  - the fader zone: a groove with the cap (30 × 46, ridged top and bottom, its bright centre line at the level, caution
//    when changed tonight, faded when not sent), the dB scale, and the meter's two bars with the clip light above them,
//    all on the fader's travel (the bottom 15% −60 to −30 dB, the next 60% −30 to 0, the top 25% 0 to +6), the travel
//    running from 10 px below the zone's top to 10 px above its bottom. The bars are the strip's colour, blending to
//    caution from −6 dB to −1 and danger from 0; a thin line holds each bar's recent peak about 1.5 s; the clip light stays
//    red 2 s after a sample over full scale. A muted or solo-silenced strip shows empty bars, and its fader and pan dimmed;
//  - the name label: a dot in the strip's colour, the name, a dim ◉ for in-ears only, and the footer ("Wood block",
//    "not sent", "+2 dB tonight" in caution; compact strips show only a warning).
// Full strips are 112 px wide, compact (the main view) 118. The selected strip has a mint outline (its glow is the row's).
//
// The mouse: a drag on the fader (anywhere left of the scale) moves the level with the mouse from wherever it was pressed,
// Shift for a quarter of the movement; a double-click sets 0 dB; the wheel over the fader zone moves 0.5 dB a notch. A
// drag sideways on the pan bar moves the pan (the bar's width is the whole range; Shift a quarter), a double-click centres
// it (not a linked one), the wheel moves it 1 a notch. onLevelDrag(db, start, end) and onPanDrag(pan, start, end) report a
// gesture as LevelFader did: a press starts it with the value as it is (0 for a linked pan), each move that changes the
// value reports it, the release ends it; a double-click or wheel notch is a gesture of its own (start and end together).
// So a press and release without a move reports no value of its own, and changes nothing. A drag is measured on the screen,
// so a press that scrolls the row to the strip (its selection) isn't read as a move. A wheel or swipe that is mostly
// sideways is passed on, so the row scrolls. M, S and the link button act on the press: onMute(!muted), onSolo(!soloed),
// onLink(!linked). Any press calls onSelect first.
//
// Typing (the mixer decides whether it may, in Edit mode only): a press on the level box asks for a typed level,
// onTypeRequest(Field::Level), at once; a click (a press and release without a move) on the pan bar's words asks for a
// typed pan, onTypeRequest(Field::Pan), once the double-click time has passed without a second press, so a double-click
// there still centres the pan. flash() shows that box (the level box, or the pan bar) danger red once, for a third of a
// second, when what was typed couldn't be read. The strip itself never takes the keyboard: the mixer does.
class ChannelStrip : public juce::Component {
public:
    enum class Size { Full, Compact };
    enum class Field { Level, Pan };                              // what a typed value sets
    static constexpr float kMinDb = -60.0f, kMaxDb = 6.0f;
    // The travel: the fraction of the way up for a level, and the level at a fraction (limited to the range).
    static float dbAtFraction(float fromBottom01);
    static float fractionForDb(float db);
    static int stripWidth(Size);                                  // 112, or 118 compact
    static int minimumHeight(Size);                               // every row, and a fader zone of 120 px

    ChannelStrip();

    void set(const MixerState::Strip&, Size);
    const MixerState::Strip& strip() const { return strip_; }
    Size stripSize() const { return size_; }
    void setSelected(bool);
    bool isSelected() const { return selected_; }

    // One frame of the meter: the largest magnitude, left and right, since the last frame (MeterReader). Each bar jumps up
    // to a louder peak and otherwise falls 1.5 dB a frame; the frames are 30 a second.
    void setMeter(app::MeterReader::Peak now);
    void setMeter(app::MeterReader::Peak now, double nowMs);      // at a chosen time, for tests and the screenshots
    void resetPeak();                                             // the peak box back to "−∞"
    void clearMeter();                                            // the bars, hold lines and clip light back to nothing
    float meterDb(int side) const { return level_[side & 1]; }    // what each bar shows (0 left, 1 right); -60 empty
    float holdDb(int side) const { return hold_[side & 1]; }      // the hold line; -60 none
    std::optional<float> peakDb() const { return peak_; }         // the peak box; nullopt: "−∞"
    bool clipLit() const { return clipLit_; }

    // The words in the boxes, as drawn: the level ("−3.0", "off"), the peak ("−∞", "+1.4"), the pan ("C", "L 40",
    // "Follow"; "" with no pan bar).
    juce::String levelText() const;
    juce::String peakText() const;
    juce::String panText() const;

    std::function<void(float db, bool gestureStart, bool gestureEnd)> onLevelDrag;
    std::function<void(int pan, bool gestureStart, bool gestureEnd)> onPanDrag;
    std::function<void(bool linked)> onLink;
    std::function<void(bool muted)> onMute;
    std::function<void(bool soloed)> onSolo;
    std::function<void()> onPeakReset, onSelect;
    std::function<void(Field)> onTypeRequest;

    void flash(Field);                                            // that box danger red once
    std::optional<Field> flashing() const { return flash_; }

    // Where each part is, in the strip's coordinates; empty when the strip has none.
    juce::Rectangle<int> panArea() const;                         // the pan bar
    juce::Rectangle<int> panBoxArea() const { return panArea(); } // where a typed pan goes
    juce::Rectangle<int> panTextArea() const;                     // the pan bar's words: a click there types a pan
    juce::Rectangle<int> linkArea() const;
    juce::Rectangle<int> muteArea() const;
    juce::Rectangle<int> soloArea() const;
    juce::Rectangle<int> levelBoxArea() const;                    // where a typed level goes
    juce::Rectangle<int> peakBoxArea() const;
    juce::Rectangle<int> faderZoneArea() const { return zone_; }
    juce::Rectangle<int> scaleArea() const;
    juce::Rectangle<int> meterArea() const;                       // both bars
    juce::Rectangle<int> clipLightArea() const;
    juce::Rectangle<int> meterFrameArea() const;                  // what a meter frame redraws: the bars, the clip light and its glow
    juce::Rectangle<int> nameArea() const { return name_; }
    float yForDb(float db) const;                                 // where the cap's centre line, the scale and the meter put a level
    juce::Rectangle<float> capBounds() const;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class Drag { None, Fader, Pan };

    bool hasPanBar() const { return strip_.pan.has_value(); }
    bool showsLink() const { return hasPanBar() && strip_.linkable; }
    bool showsSolo() const { return strip_.canSolo && size_ == Size::Full; }
    bool silent() const { return strip_.muted || strip_.silencedBySolo; }
    juce::Rectangle<int> faderArea() const;                       // what a fader drag starts on
    float travel() const;                                         // the pixels from -60 to +6
    float currentDb() const;
    int notchesOf(const juce::MouseWheelDetails&);

    MixerState::Strip strip_;
    Size size_ = Size::Full;
    bool selected_ = false;

    juce::Rectangle<int> panRow_, buttonRow_, boxRow_, zone_, name_;

    float level_[2] = {kMinDb, kMinDb}, hold_[2] = {kMinDb, kMinDb};
    double holdUntil_[2] = {0.0, 0.0}, clipUntil_ = -1.0;
    std::optional<float> peak_;
    bool clipLit_ = false;

    Drag drag_ = Drag::None;
    float dragFraction_ = 0.0f, dragPan_ = 0.0f, lastMouse_ = 0.0f, wheelAccumulated_ = 0.0f;
    bool panPressOnWords_ = false, panMoved_ = false;              // this pan press: on the words? moved since?
    int typeTicket_ = 0, flashTicket_ = 0;                        // the latest pending pan type request, and flash
    std::optional<Field> flash_;
};

}  // namespace sqlive::ui
