#include "ui/fx/ControlRow.h"

#include <algorithm>
#include <cmath>
#include <string_view>

#include "ui/fx/params/Parameters.h"
#include "ui/fx/ChainHost.h"
#include "ui/fx/ChoiceStrip.h"
#include "ui/fx/GraphMapping.h"
#include "ui/fx/OutputControls.h"
#include "ui/fx/SmartQLookAndFeel.h"
#include "ui/fx/Theme.h"

namespace smartq {

namespace {

using Apvts = juce::AudioProcessorValueTreeState;

// A cell for each control, from the left, with the room between them shared out up to a limit. Two rows take smaller knobs, so that
// a page at the smallest window keeps the room it needs above them.
constexpr int kPad = 9, kMargin = 12, kRowGap = 5, kCaptionHeight = 14, kCaptionPad = 4;
constexpr int kOneRowKnob = 58, kTwoRowKnob = 50;
constexpr int kKnobColumn = 72, kSwitchColumn = 64, kSwitchWidth = 52, kSwitchHeight = 18;
constexpr int kStripHeight = 25, kStripGap = 2, kWarningHeight = 14, kSegmentPad = 18, kMinSegment = 44;
constexpr int kNoteGap = 2, kNoteWidth = 76;
constexpr int kMinGap = 8, kMaxGap = 26;

bool straddlesZero(const ControlSpec& spec) { return spec.minimum < 0.0 && spec.maximum > 0.0; }

bool isBalance(const ControlSpec& spec) { return std::string_view(spec.field) == "balance"; }

// A ratio, a Multiband band's "band1_ratio" among them, says that it is one, as Dynamic's page does: "3.0 :1".
bool isRatio(const ControlSpec& spec) { return std::string_view(spec.field).ends_with("ratio"); }

// A Width's Balance by the side it leans to: "L 26", "Centre", "R 40".
juce::String balanceText(double value) {
    const int amount = juce::roundToInt(value);
    if (amount == 0) return "Centre";
    return (amount < 0 ? "L " : "R ") + juce::String(std::abs(amount));
}

// The same back, with a plain number taken as it is: left is below 0.
double balanceValue(const juce::String& text) {
    const auto trimmed = text.trim();
    const auto first = juce::CharacterFunctions::toLowerCase(trimmed[0]);
    if (first == 'c') return 0.0;
    if (first == 'l') return -std::abs(trimmed.substring(1).getDoubleValue());
    if (first == 'r') return std::abs(trimmed.substring(1).getDoubleValue());
    return trimmed.getDoubleValue();
}

// A value in the control's unit: a gain that can go either way with its sign, as the EQ writes one.
juce::String textFor(const ControlSpec& spec, double value) {
    if (isBalance(spec)) return balanceText(value);
    const std::string_view unit = spec.unit;
    if (unit == "dB") return spec.raisesLevel || straddlesZero(spec) ? formatGain(value) : juce::String(value, 1) + " dB";
    if (unit == "ms") return formatMilliseconds(value);
    if (unit == "Hz") return formatFrequency(value);
    if (unit == "%") return juce::String(juce::roundToInt(value)) + " %";
    if (isRatio(spec)) return juce::String(value, 1) + " :1";
    return juce::String(value, 1);
}

double valueFor(const ControlSpec& spec, const juce::String& text) {
    if (isBalance(spec)) return balanceValue(text);
    const std::string_view unit = spec.unit;
    if (unit == "ms") return parseMilliseconds(text);
    if (unit == "Hz") return parseFrequency(text);
    return text.getDoubleValue();
}

juce::StringArray labelsOf(const ControlSpec& spec) {
    juce::StringArray labels;
    for (const char* label : spec.choices) labels.add(label);
    return labels;
}

juce::Font stripFont() { return theme::font(theme::kSizeCaption, true); }

int segmentWidthFor(float wordsWidth) { return std::max(kMinSegment, juce::roundToInt(std::ceil(wordsWidth)) + kSegmentPad); }

// Its labels' strip: each segment as wide as the widest label's.
int stripWidthFor(const ControlSpec& spec) {
    float widest = 0.0f;
    for (const char* label : spec.choices) widest = std::max(widest, juce::GlyphArrangement::getStringWidth(stripFont(), label));
    return segmentWidthFor(widest) * static_cast<int>(spec.choices.size());
}

// The strip of words the owner gave: each segment as wide as its own words.
std::vector<int> segmentWidthsFor(const juce::StringArray& words) {
    std::vector<int> widths;
    for (const auto& word : words) widths.push_back(segmentWidthFor(juce::GlyphArrangement::getStringWidth(stripFont(), word)));
    return widths;
}

bool isListen(const ControlSpec& spec) { return std::string_view(spec.field) == "listen"; }

// A switch that says whether it is on or off, so that it reads right either way, whatever changed it: a click, the host or the AI.
// Its attachment sets it with a notification, and every change of its state comes through here.
class StateSwitch final : public juce::ToggleButton {
public:
    StateSwitch() { followState(); }

    void buttonStateChanged() override {
        juce::ToggleButton::buttonStateChanged();
        followState();
    }

private:
    void followState() { setButtonText(getToggleState() ? "On" : "Off"); }
};

}  // namespace

ControlRow::ControlRow(ChainHost& processor, int slot, std::initializer_list<const char*> fields)
    : processor_(processor), slot_(slot), arrival_(processor.audioBlocksProcessed()) {
    auto& apvts = processor_.apvts();
    const auto type = slotInfo(slot).type;
    const auto accent = theme::moduleAccent(type);
    const auto table = controlsOf(type);

    for (const char* field : fields) {
        const int index = indexOfControl(table, field);
        jassert(index >= 0);
        if (index < 0) continue;

        Control control;
        control.spec = &table[static_cast<size_t>(index)];
        const auto& spec = *control.spec;
        control.field = spec.field;
        control.kind = spec.kind;
        const auto id = moduleParamId(slot_, spec.field);

        switch (spec.kind) {
            case ControlKind::Float:
                control.component = std::make_unique<ParameterKnob>(
                    apvts, id, [&spec](double v) { return textFor(spec, v); }, [&spec](const juce::String& t) { return valueFor(spec, t); }, accent);
                break;
            case ControlKind::Bool: {
                auto button = std::make_unique<StateSwitch>();
                button->getProperties().set(SmartQLookAndFeel::accentSwitchProperty, true);
                button->setColour(juce::ToggleButton::tickColourId, accent);
                control.switchAttachment = std::make_unique<Apvts::ButtonAttachment>(apvts, id, *button);
                control.component = std::move(button);
                break;
            }
            case ControlKind::Choice: {
                const auto labels = labelsOf(spec);
                auto strip = std::make_unique<ChoiceStrip>(labels, std::vector<float>{}, wordPainter(labels));
                strip->setAccent(accent);
                strip->attachTo(apvts.getParameter(id));
                control.component = std::move(strip);
                if (isListen(spec)) listen_ = apvts.getRawParameterValue(id);
                break;
            }
        }
        control.component->setName(spec.hostName);
        addAndMakeVisible(*control.component);
        addCaption(control, spec.hostName);
        controls_.push_back(std::move(control));
    }

    nothingRouted_.setTooltip("Route another track to Smart Q's sidechain input in the host");
    addChildComponent(nothingRouted_);
    nothingArriving_.setTooltip("The sidechain input is on, and nothing has come in on it. In the host, send a track to Smart Q's sidechain");
    addChildComponent(nothingArriving_);
}

ControlRow::~ControlRow() {
    for (auto& control : controls_)
        if (auto* strip = dynamic_cast<ChoiceStrip*>(control.component.get())) strip->attachTo(nullptr);
}

void ControlRow::addCaption(Control& control, const juce::String& text) {
    control.caption = std::make_unique<juce::Label>();
    control.caption->setText(text.toUpperCase(), juce::dontSendNotification);
    control.caption->setFont(theme::font(theme::kSizeCaption, true).withExtraKerningFactor(0.06f));
    control.caption->setJustificationType(control.kind == ControlKind::Choice ? juce::Justification::centredLeft : juce::Justification::centred);
    control.caption->setBorderSize({});
    control.caption->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*control.caption);
}

ControlRow::Control* ControlRow::find(const char* field) noexcept {
    for (auto& control : controls_)
        if (control.field == field) return &control;
    return nullptr;
}

juce::Button& ControlRow::addSwitch(const char* field, const juce::String& caption) {
    jassert(find(field) == nullptr);
    Control control;
    control.field = field;
    auto button = std::make_unique<StateSwitch>();
    button->getProperties().set(SmartQLookAndFeel::accentSwitchProperty, true);
    button->setColour(juce::ToggleButton::tickColourId, theme::moduleAccent(slotInfo(slot_).type));
    button->setName(caption);
    auto& made = *button;
    control.component = std::move(button);
    addAndMakeVisible(made);
    addCaption(control, caption);
    controls_.push_back(std::move(control));
    resized();
    return made;
}

juce::Component* ControlRow::controlFor(const char* field) noexcept {
    auto* control = find(field);
    return control != nullptr ? control->component.get() : nullptr;
}

juce::Label* ControlRow::captionFor(const char* field) noexcept {
    auto* control = find(field);
    return control != nullptr ? control->caption.get() : nullptr;
}

void ControlRow::setTooltip(const char* field, const juce::String& tooltip) {
    auto* control = controlFor(field);
    if (auto* client = dynamic_cast<juce::SettableTooltipClient*>(control)) client->setTooltip(tooltip);
    if (auto* strip = dynamic_cast<ChoiceStrip*>(control)) strip->setTooltip(tooltip);
}

juce::Label& ControlRow::addNote(const char* field, const juce::String& tooltip) {
    auto* control = find(field);
    jassert(control != nullptr && control->note == nullptr);
    auto note = std::make_unique<juce::Label>();
    note->setName(control->component->getName() + " note");
    note->setFont(theme::font(theme::kSizeSmall));
    note->setColour(juce::Label::textColourId, theme::textMid);
    note->setJustificationType(juce::Justification::centredLeft);
    note->setBorderSize({});
    note->setInterceptsMouseClicks(false, false);
    note->setTooltip(tooltip);
    addChildComponent(*note);
    control->note = std::move(note);
    resized();
    return *control->note;
}

void ControlRow::update(double dtSeconds) {
    // As the spec has it: Sidechain chosen, and the host's sidechain bus not on.
    const bool sidechain = listen_ != nullptr && static_cast<int>(std::lround(listen_->load())) == static_cast<int>(dsp::ListenTo::Sidechain);
    nothingRouted_.setVisible(sidechain && !processor_.sidechainRouted());
    nothingArriving_.setVisible(arrival_.update(processor_, sidechain, dtSeconds));
}

void ControlRow::setChoiceWords(const char* field, const juce::StringArray& words) {
    auto* control = find(field);
    jassert(control != nullptr && control->kind == ControlKind::Choice && words.size() == static_cast<int>(control->spec->choices.size()));
    if (control == nullptr || control->words == words) return;
    control->words = words;
    std::vector<float> weights;
    for (const int width : segmentWidthsFor(words)) weights.push_back(static_cast<float>(width));
    dynamic_cast<ChoiceStrip&>(*control->component).setWords(words, std::move(weights));
    resized();
}

juce::StringArray ControlRow::choiceWords(const char* field) {
    const auto* control = find(field);
    if (control == nullptr || control->kind != ControlKind::Choice) return {};
    return control->words.isEmpty() ? labelsOf(*control->spec) : control->words;
}

int ControlRow::stripWidth(const Control& control) const {
    if (control.words.isEmpty()) return stripWidthFor(*control.spec);
    int width = 0;
    for (const int segment : segmentWidthsFor(control.words)) width += segment;
    return width;
}

int ControlRow::cellWidth(const Control& control) const {
    int width = kKnobColumn;
    if (control.kind == ControlKind::Bool) width = kSwitchColumn;
    if (control.kind == ControlKind::Choice) {
        const auto& spec = *control.spec;
        width = stripWidth(control);
        if (isListen(spec)) width = std::max({width, nothingRouted_.idealWidth(), nothingArriving_.idealWidth()});
    }
    // Its caption in full, too.
    const auto& caption = *control.caption;
    width = std::max(width, juce::roundToInt(std::ceil(juce::GlyphArrangement::getStringWidth(caption.getFont(), caption.getText()))) + kCaptionPad);
    return width + (control.note != nullptr ? kNoteGap + kNoteWidth : 0);
}

int ControlRow::naturalWidth(size_t from, size_t to) const {
    int width = 0;
    for (size_t i = from; i < to; ++i) width += cellWidth(controls_[i]);
    return width + static_cast<int>(std::max<size_t>(1, to - from) - 1) * kMinGap;
}

bool ControlRow::fitsOneRow(int width) const { return naturalWidth(0, controls_.size()) <= width - 2 * kMargin; }

int ControlRow::heightFor(int width) const {
    const bool one = fitsOneRow(width);
    const int rows = one ? 1 : 2;
    return 2 * kPad + rows * (kCaptionHeight + (one ? kOneRowKnob : kTwoRowKnob)) + (rows - 1) * kRowGap;
}

void ControlRow::placeRow(size_t from, size_t to, juce::Rectangle<int> row, int knobSize) {
    const int count = static_cast<int>(to - from);
    const int spare = row.getWidth() - naturalWidth(from, to) + (count - 1) * kMinGap;
    const int gap = count > 1 ? juce::jlimit(kMinGap, kMaxGap, spare / (count - 1)) : 0;

    for (size_t i = from; i < to; ++i) {
        auto& control = controls_[i];
        auto cell = row.removeFromLeft(cellWidth(control));
        row.removeFromLeft(gap);
        if (control.note != nullptr) {
            const auto note = cell.removeFromRight(kNoteWidth);
            cell.removeFromRight(kNoteGap);
            control.note->setBounds(note.getX(), note.getY() + kCaptionHeight + (knobSize - kCaptionHeight) / 2, kNoteWidth, kCaptionHeight + 2);
        }

        control.caption->setBounds(cell.getX(), cell.getY(), cell.getWidth(), kCaptionHeight);
        const int top = cell.getY() + kCaptionHeight;
        switch (control.kind) {
            case ControlKind::Float:
                control.component->setBounds(cell.getX() + (cell.getWidth() - knobSize) / 2, top, knobSize, knobSize);
                break;
            case ControlKind::Bool:
                control.component->setBounds(cell.getX() + (cell.getWidth() - kSwitchWidth) / 2, top + (knobSize - kSwitchHeight) / 2, kSwitchWidth, kSwitchHeight);
                break;
            case ControlKind::Choice: {
                const auto& spec = *control.spec;
                control.component->setBounds(cell.getX(), top, stripWidth(control), kStripHeight);
                if (isListen(spec))
                    for (auto* note : {&nothingRouted_, &nothingArriving_})
                        note->setBounds(cell.getX(), top + kStripHeight + kStripGap, std::max(stripWidth(control), note->idealWidth()), kWarningHeight);
                break;
            }
        }
    }
}

void ControlRow::resized() {
    auto area = getLocalBounds().reduced(kMargin, kPad);
    if (fitsOneRow(getWidth())) {
        placeRow(0, controls_.size(), area.removeFromTop(kCaptionHeight + kOneRowKnob), kOneRowKnob);
        return;
    }
    const size_t half = (controls_.size() + 1) / 2;
    placeRow(0, half, area.removeFromTop(kCaptionHeight + kTwoRowKnob), kTwoRowKnob);
    area.removeFromTop(kRowGap);
    placeRow(half, controls_.size(), area.removeFromTop(kCaptionHeight + kTwoRowKnob), kTwoRowKnob);
}

}  // namespace smartq
