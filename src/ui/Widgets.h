#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/Theme.h"

namespace sqlive::ui {

// The app's shared controls, each drawn from the theme as the mockup's CSS draws it. Each one that takes a value
// reports the user's changes through a std::function and never calls it for a change made in code. A disabled one
// is drawn at 40% (the mockup's .off) and ignores the mouse. idealWidth() is the width the mockup gives it.

// The mockup's .btn: 13 px semibold text on a field with a hairline border, 8 px corners, 12 px either side.
// Primary is .btn.pri (mint, onAccent text), Ember .btn.emb, Danger .btn.red (danger text on the field). A Plain one whose
// toggle state is on is drawn open: its text bright, a 2 px mint line along its foot (the top bar's Snapshots, screen 15).
class PillButton : public juce::Button {
public:
    enum class Style { Plain, Primary, Ember, Danger };
    explicit PillButton(const juce::String& text, Style = Style::Plain);

    void setStyle(Style);
    Style style() const { return style_; }
    int idealWidth() const;
    void setPadding(int pixels);                                  // either side of the words: the mockup's 12 unless set
    // A right-click (a popup-menu click) calls this in place of a press, when it is set; unset, it presses as any click does.
    std::function<void()> onMenu;

    void paintButton(juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    Style style_;
    int padding_ = 12;
};

// The mockup's .chip: "128 BPM" with the value bright and semibold and the label in mid, on a field with a hairline
// border. The value comes first unless labelFirst ("Key E", "Count-in 1 bar"); an empty label shows the value alone ("4/4").
// Compact, it is the Song editor bar's .ebar .chip: 12 px words, 8 px either side (else 13 px and 10 px).
class Chip : public juce::Component {
public:
    Chip();
    void setText(const juce::String& label, const juce::String& value, bool labelFirst = false);
    void setCompact(bool);
    int idealWidth() const;
    void paint(juce::Graphics&) override;

private:
    float fontSize() const { return compact_ ? theme::sizeSmall : theme::sizeBody; }
    float padding() const { return compact_ ? 8.0f : 10.0f; }

    juce::String label_, value_;
    bool labelFirst_ = false, compact_ = false;
};

// The mockup's .st: a 12 px pill in mint (Ok), caution (Warning) or danger (Bad), its tint behind the words.
class StatusPill : public juce::Component, public juce::SettableTooltipClient {
public:
    enum class Level { Ok, Warning, Bad };
    StatusPill();
    void set(const juce::String&, Level);
    const juce::String& text() const { return text_; }
    Level level() const { return level_; }
    int idealWidth() const;
    void paint(juce::Graphics&) override;

private:
    juce::String text_;
    Level level_ = Level::Ok;
};

// The mockup's h4: 11 px, bold, letter-spaced 0.8 px, in dim (or another colour), in capitals, left-aligned.
class CapsLabel : public juce::Component {
public:
    explicit CapsLabel(const juce::String& = {});
    void setText(const juce::String&);
    void setTextColour(juce::Colour);
    int idealWidth() const;
    void paint(juce::Graphics&) override;

private:
    juce::String text_;
    juce::Colour colour_;
};

// The mockup's dropdown (.dd): the chosen item on a field with a hairline border, 6 px corners and a chevron; a click
// opens a popup menu of the items with the chosen one ticked. selected() is -1 when nothing is chosen.
// setNeedsAttention is .dd.need: the border and the words in caution, for a choice still to be made. With nothing chosen
// it shows its placeholder ("Choose a port…"), in dim, or in caution when it needs attention. setDots gives items the
// mockup's .dd i: an 8 px dot in that colour before the chosen item's words (a transparent colour, or none given: no dot).
class Choice : public juce::Component {
public:
    Choice();
    void setItems(const juce::StringArray&, int selected);
    void setSelected(int);
    int selected() const { return selected_; }
    const juce::StringArray& items() const { return items_; }
    void setNeedsAttention(bool);
    void setPlaceholder(const juce::String&);
    void setDots(std::vector<juce::Colour>);                      // one per item, in the items' order
    void setBold(bool);                                           // semibold words, as a .dd with font-weight 600
    const juce::String& placeholder() const { return placeholder_; }
    std::function<void(int)> onChange;

    int idealWidth() const;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

private:
    void choose(int);
    float dotRoom(int item) const;                                // the dot and its gap; 0 when the item has none

    juce::StringArray items_;
    int selected_ = -1;
    bool needsAttention_ = false;
    juce::String placeholder_;
    std::vector<juce::Colour> dots_;
    bool bold_ = false;
    juce::Font font() const;
};

// The mockup's .tools switch: segments on a field, the chosen one raised on the popup colour in bright text. The
// segments share the width, each its text's width plus an equal share of what is left. A press chooses.
class Segmented : public juce::Component {
public:
    Segmented();
    void setItems(const juce::StringArray&, int selected);
    void setSelected(int);
    int selected() const { return selected_; }
    std::function<void(int)> onChange;

    int idealWidth() const;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    std::vector<juce::Rectangle<float>> segments() const;
    int segmentAt(juce::Point<float>) const;

    juce::StringArray items_;
    int selected_ = -1, hovered_ = -1;
};

// The mockup's .tog: a 34 x 18 switch, its knob dim on the left when off, mint on the right when on. A press flips it.
class Switch : public juce::Component {
public:
    Switch();
    void setOn(bool);
    bool isOn() const { return on_; }
    std::function<void(bool)> onChange;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    bool on_ = false;
};

// The mockup's .fader: an 8 px track, the meter filling it from the bottom in the strip's colour, and a 32 x 14 cap,
// white, or caution when the level was changed tonight. A level that isn't sent (nullopt) puts the cap at the bottom, dimmed.
// A drag moves the cap with the mouse from wherever it is pressed (Shift for a quarter of the movement); a double-click
// sets 0 dB; the mouse wheel moves 0.5 dB a notch. Levels are reported to a tenth of a dB, from -60 ("off") to +6.
// onDrag(db, gestureStart, gestureEnd): a press starts a gesture with the current level, each move that changes it
// reports it, the release ends it; a double-click or a wheel notch is a gesture of its own (start and end together).
class LevelFader : public juce::Component {
public:
    static constexpr float kMinDb = -60.0f, kMaxDb = 6.0f;

    LevelFader();
    void setValue(std::optional<float> db);
    std::optional<float> value() const { return value_; }
    using juce::Component::setColour;
    void setColour(juce::Colour);
    void setChangedTonight(bool);
    void setMeter(float gain01);                  // the playing level as a linear gain, 1 = full scale
    std::function<void(float db, bool gestureStart, bool gestureEnd)> onDrag;

    // The travel: the bottom 15% covers -60 to -30 dB, the next 60% -30 to 0 dB, the top 25% 0 to +6 dB.
    static float dbAtFraction(float fromBottom01);
    static float fractionForDb(float db);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    float travel() const;                        // the pixels the cap's centre moves from -60 to +6
    float yForFraction(float) const;
    float currentDb() const;
    void report(float db, bool start, bool end);

    std::optional<float> value_;
    juce::Colour colour_ = theme::mid;
    bool changedTonight_ = false;
    float meter_ = 0.0f;
    bool dragging_ = false;
    float dragFraction_ = 0.0f, lastY_ = 0.0f, wheelAccumulated_ = 0.0f;
};

// The mockup's kbd: the key's name, 11 px semibold, on a field with a hairline border, 2 px at the bottom, 4 px corners,
// 18 px high and centred on centreY, starting at x. Returns its width.
float drawKey(juce::Graphics&, const juce::String& key, float x, float centreY);
float keyWidth(const juce::String& key);

// A key hint as the mockups write it ("hold Space stop", "← → next song"): the word before its keys, its keys drawn as
// keys side by side 3 px apart, then its words, in dim in that font, 4 px from the keys. The keys are carried apart from
// the words, so a learned key ("Q", "F5", "Ctrl+Z") is drawn as a key like the defaults.
struct KeyHint {
    juce::String before;                                          // "hold"; "" for none
    juce::StringArray keys;                                       // {"←", "→"}, {"Space"}, {"Esc", "Esc"}
    juce::String words;                                           // "next song"
    bool range = false;                                           // two keys and those between: "1–9", a dash between them
    juce::String text() const;                                    // the whole, as one line: "hold Space stop", "1–9 start …"
    bool operator==(const KeyHint&) const = default;
};
juce::StringArray textsOf(const std::vector<KeyHint>&);
float hintWidth(const KeyHint&, const juce::Font&);
void drawHint(juce::Graphics&, const KeyHint&, const juce::Font&, float x, float centreY);

// The mockup's .beats: one light a beat of the bar, the current one mint with a glow, the others on a field with a
// hairline border. 12 px in the top bar, 34 px on the stage view; the gap and the glow scale with them.
class BeatLights : public juce::Component {
public:
    BeatLights();
    void set(int beatsInBar, int beatNow /* 1-based, 0 = none */, float diameter);
    int idealWidth() const;
    void paint(juce::Graphics&) override;

private:
    float gap() const;

    int beats_ = 4, now_ = 0;
    float diameter_ = 12.0f;
};

}  // namespace sqlive::ui
