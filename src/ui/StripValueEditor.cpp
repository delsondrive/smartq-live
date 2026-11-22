#include "ui/StripValueEditor.h"

#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's CSS: .num (background #0b0a0e, 4 px corners) and .num.typing (a 1 px mint border, a 2 px mint ring at 25%,
// the words from 6 px in, a mint caret).
constexpr int kRing = 2, kIndent = 6;
constexpr float kRadius = 4.0f;
const juce::Colour kRecess{0xff0b0a0e};

}  // namespace

// The text field itself: Enter and Esc answered at once (a TextEditor would post them for later), and the keyboard
// leaving it read as "set it".
class StripValueEditor::Field : public juce::TextEditor {
public:
    explicit Field(StripValueEditor& owner) : owner_(owner) {}

    bool keyPressed(const juce::KeyPress& key) override {
        if (key.getKeyCode() == juce::KeyPress::returnKey) {
            owner_.done(true);
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::escapeKey) {
            owner_.done(false);
            return true;
        }
        return juce::TextEditor::keyPressed(key);
    }

    void focusLost(FocusChangeType cause) override {
        juce::TextEditor::focusLost(cause);
        owner_.done(true);
    }

private:
    StripValueEditor& owner_;
};

StripValueEditor::StripValueEditor() : field_editor_(std::make_unique<Field>(*this)) {
    auto& f = *field_editor_;
    f.setFont(theme::font(theme::sizeCaption));
    f.setIndents(kIndent - 1, 0);
    f.setJustification(juce::Justification::centredLeft);
    f.setBorder({});
    f.setSelectAllWhenFocused(true);
    f.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    f.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    f.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    f.setColour(juce::TextEditor::textColourId, theme::text);
    f.setColour(juce::TextEditor::highlightColourId, theme::mint.withAlpha(0.3f));
    f.setColour(juce::TextEditor::highlightedTextColourId, theme::text);
    f.setColour(juce::CaretComponent::caretColourId, theme::mint);
    addAndMakeVisible(f);
    setInterceptsMouseClicks(false, true);
}

StripValueEditor::~StripValueEditor() {
    open_ = false;                                                // a focus change while going is no "set it"
}

void StripValueEditor::start(ChannelStrip& strip, ChannelStrip::Field field, juce::Rectangle<int> box, const juce::String& text) {
    stop();
    field_ = field;
    field_editor_->setText(text, juce::dontSendNotification);
    strip.addAndMakeVisible(*this);
    place(box);
    open_ = true;
    field_editor_->selectAll();
    if (field_editor_->isShowing()) field_editor_->grabKeyboardFocus();
}

void StripValueEditor::stop() {
    open_ = false;
    if (auto* parent = getParentComponent()) parent->removeChildComponent(this);
}

void StripValueEditor::place(juce::Rectangle<int> box) { setBounds(box.expanded(kRing)); }

juce::String StripValueEditor::text() const { return field_editor_->getText(); }

void StripValueEditor::setText(const juce::String& text) { field_editor_->setText(text, juce::dontSendNotification); }

bool StripValueEditor::press(const juce::KeyPress& key) { return field_editor_->keyPressed(key); }

void StripValueEditor::keyboardWentElsewhere() { done(true); }

void StripValueEditor::done(bool keep) {
    if (!open_) return;
    open_ = false;
    if (onDone) onDone(keep);
}

void StripValueEditor::paint(juce::Graphics& g) {
    const auto box = getLocalBounds().toFloat().reduced(static_cast<float>(kRing));
    g.setColour(theme::mint.withAlpha(0.25f));                    // the ring
    g.fillRoundedRectangle(box.expanded(static_cast<float>(kRing)), kRadius + kRing);
    g.setColour(kRecess);
    g.fillRoundedRectangle(box, kRadius);
    g.setColour(theme::mint);
    g.drawRoundedRectangle(box.reduced(0.5f), kRadius, 1.0f);
}

void StripValueEditor::resized() { field_editor_->setBounds(getLocalBounds().reduced(kRing + 1)); }

}  // namespace sqlive::ui
