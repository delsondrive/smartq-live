#include "ui/MidiImportDialog.h"

#include <algorithm>
#include <cmath>

#include "model/JsonObject.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

const juce::String kTick = juce::String::fromUTF8("\xe2\x9c\x93");         // ✓
const char* const kTickWords[3] = {"Tempo", "Sections", "Pedal changes"};

// The mockup's dialog CSS: .dlg (14 px corners), .dh (14 px 18 px, an 18 px title), the body's 16 px 18 px, .df (12 px
// 18 px, 10 px gaps) and .check (a 16 px box, 6 px from its words).
constexpr float kRadius = 14.0f;
constexpr int kPadX = 18, kHeaderHeight = 58, kBodyPadY = 16;
constexpr int kFooterPadY = 12, kButtonHeight = 30, kLineHeight = 20, kFooterGap = 10, kTickSize = 16, kTickGap = 6;

juce::Font titleFont() { return theme::font(18.0f, true); }
juce::Font bodyFont() { return theme::font(theme::sizeBody); }
juce::Font hintFont() { return theme::font(theme::sizeSmall); }

int tickWidth(int i) { return kTickSize + kTickGap + static_cast<int>(std::ceil(widthOf(bodyFont(), kTickWords[i]))); }

}  // namespace

MidiImportDialog::MidiImportDialog(app::Session& session, juce::String songId, juce::File midiFile)
    : session_(session), songId_(std::move(songId)), file_(std::move(midiFile)), panel_(session) {
    setWantsKeyboardFocus(true);
    model::MidiFileContent content;
    const auto read = model::readMidiFile(file_, content);
    if (read.wasOk()) {
        choice_ = model::defaultMidiChoice(content, session_.model().band());
        content_ = std::move(content);
    } else {
        problem_ = read.getErrorMessage();
    }

    view_.setViewedComponent(&panel_, false);
    view_.setScrollBarsShown(true, false);
    view_.setScrollBarThickness(10);
    panel_.onChoiceChanged = [this](const model::MidiImportChoice& choice) {
        choice_.trackToDevice = choice.trackToDevice;
        update();
    };
    cancel_.onClick = [this] {
        if (onClose) onClose();
    };
    import_.onClick = [this] { import(); };
    for (juce::Component* child : std::initializer_list<juce::Component*>{&view_, &cancel_, &import_}) addAndMakeVisible(child);
    update();
}

void MidiImportDialog::setTicks(bool tempo, bool sections, bool pedalChanges) {
    tempo_ = tempo;
    sections_ = sections;
    pedalChanges_ = pedalChanges;
    update();
}

void MidiImportDialog::import() {
    if (!content_ || session_.mode() != app::Mode::Edit) return;
    auto choice = choice_;
    choice.tempo = tempo_;
    choice.sections = sections_;
    if (!pedalChanges_) choice.trackToDevice.clear();
    const auto result = session_.commands().run(
        "song.importMidi", model::jsonObject({{"songId", songId_}, {"content", model::toVar(*content_)}, {"choice", model::toVar(choice)}}));
    if (!result.ok) {
        refused_ = result.message;
        update();
        return;
    }
    if (onClose) onClose();                                       // last: it may close the dialog
}

void MidiImportDialog::update() {
    panel_.setProblem(problem_);
    panel_.setContent(file_, content_ ? &*content_ : nullptr, choice_);
    import_.setEnabled(content_.has_value() && session_.mode() == app::Mode::Edit);
    if (auto* parent = getParentComponent()) parent->resized();  // the veil sizes it from idealHeight()
    resized();
    repaint();
}

int MidiImportDialog::idealHeight() const {
    const int footer = 2 * kFooterPadY + kButtonHeight + (refused_.isNotEmpty() ? kLineHeight : 0);
    return kHeaderHeight + 2 * kBodyPadY + panel_.idealHeight(width - 2 * kPadX) + footer;
}

void MidiImportDialog::resized() {
    auto area = getLocalBounds();
    header_ = area.removeFromTop(kHeaderHeight);
    footer_ = area.removeFromBottom(2 * kFooterPadY + kButtonHeight + (refused_.isNotEmpty() ? kLineHeight : 0));
    auto foot = footer_.reduced(kPadX, kFooterPadY);
    refusedLine_ = foot.removeFromTop(foot.getHeight() - kButtonHeight);
    import_.setBounds(foot.removeFromRight(import_.idealWidth()));
    foot.removeFromRight(kFooterGap);
    cancel_.setBounds(foot.removeFromRight(cancel_.idealWidth()));
    for (int i = 0; i < 3; ++i) {
        ticks_[i] = foot.removeFromLeft(std::min(tickWidth(i), foot.getWidth()));
        foot.removeFromLeft(std::min(kFooterGap + 4, foot.getWidth()));
    }

    const auto body = area.reduced(kPadX, kBodyPadY);
    const int panelHeight = panel_.idealHeight(body.getWidth());
    view_.setBounds(body.withHeight(std::min(body.getHeight(), panelHeight)));
    const int panelWidth = panelHeight > view_.getHeight() ? body.getWidth() - view_.getScrollBarThickness() : body.getWidth();
    panel_.setSize(panelWidth, panel_.idealHeight(panelWidth));
}

void MidiImportDialog::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(theme::popup);
    g.fillRoundedRectangle(bounds, kRadius);
    g.setColour(theme::hairline);
    g.drawRoundedRectangle(bounds.reduced(0.5f), kRadius, 1.0f);
    g.fillRect(1, header_.getBottom() - 1, getWidth() - 2, 1);
    g.fillRect(1, footer_.getY(), getWidth() - 2, 1);

    g.setColour(theme::text);
    g.setFont(titleFont());
    g.drawText("Import a MIDI file", header_.reduced(kPadX, 0).toFloat(), juce::Justification::centredLeft, false);

    if (refused_.isNotEmpty()) {
        g.setColour(theme::danger);
        g.setFont(hintFont());
        g.drawText(shorten(refused_, hintFont(), static_cast<float>(refusedLine_.getWidth())), refusedLine_.toFloat(),
                   juce::Justification::centredLeft, false);
    }

    const bool on[3] = {tempo_, sections_, pedalChanges_};
    for (int i = 0; i < 3; ++i) {                                 // the mockup's .check
        if (ticks_[i].isEmpty()) continue;
        const auto box = ticks_[i].toFloat().removeFromLeft(static_cast<float>(kTickSize)).withSizeKeepingCentre(kTickSize, kTickSize);
        if (on[i]) {
            g.setColour(theme::mint);
            g.fillRoundedRectangle(box, 4.0f);
            g.setColour(theme::onAccent);
            g.setFont(theme::font(theme::sizeCaption, true));
            g.drawText(kTick, box, juce::Justification::centred, false);
        } else {
            g.setColour(theme::field);
            g.fillRoundedRectangle(box, 4.0f);
            g.setColour(theme::hairline);
            g.drawRoundedRectangle(box.reduced(0.5f), 4.0f, 1.0f);
        }
        const auto label = ticks_[i].toFloat().withTrimmedLeft(static_cast<float>(kTickSize + kTickGap));
        g.setColour(theme::mid);
        g.setFont(bodyFont());
        g.drawText(shorten(kTickWords[i], bodyFont(), label.getWidth()), label, juce::Justification::centredLeft, false);
    }
}

void MidiImportDialog::mouseUp(const juce::MouseEvent& e) {
    if (e.mouseWasDraggedSinceMouseDown()) return;
    for (int i = 0; i < 3; ++i)
        if (ticks_[i].contains(e.getPosition())) {
            if (i == 0) tempo_ = !tempo_;
            else if (i == 1) sections_ = !sections_;
            else pedalChanges_ = !pedalChanges_;
            repaint(footer_);
        }
}

bool MidiImportDialog::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (onClose) onClose();
        return true;
    }
    return false;
}

}  // namespace sqlive::ui
