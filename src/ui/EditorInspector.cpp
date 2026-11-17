#include "ui/EditorInspector.h"

#include <algorithm>
#include <initializer_list>
#include <utility>

#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/MidiLane.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's .insp and its .cards: 12 px inside the column, 10 px between cards, 12 by 10 px inside each, 30 px rows.
constexpr int kPad = 12, kCardGap = 10, kCardPadX = 12, kCardPadY = 10, kCapsHeight = 14, kCapsGap = 6, kCardRow = 30;
constexpr int kValueWidth = 96, kFieldHeight = 24, kChoiceWidth = 130, kLabelGap = 10;

const juce::StringArray kSoundWords{"Wood block", "Beep", "Rim"}, kSubdivisionWords{"None", "8ths", "Triplets", "16ths"};

juce::Font bodyFont() { return theme::font(theme::sizeBody); }

}  // namespace

EditorInspector::EditorInspector(app::Session& session)
    : session_(session), markerInspector_(session), sectionCard_(session), jumpCard_(session) {
    addChildComponent(tempoCard_);
    markerInspector_.onSelect = [this](EditorSelection picked) {
        if (onSelect) onSelect(std::move(picked));
    };
    markerView_.setViewedComponent(&markerInspector_, false);
    markerView_.setScrollBarsShown(true, false);
    markerView_.setScrollBarThickness(10);
    addChildComponent(markerView_);
    sectionCard_.onRemoved = [this] {
        if (onSelect) onSelect({});
    };
    addChildComponent(sectionCard_);
    addChildComponent(jumpCard_);

    barOneEditor_.setFont(theme::font(theme::sizeSmall));
    barOneEditor_.setIndents(8, 3);
    barOneEditor_.setJustification(juce::Justification::centredRight);
    barOneEditor_.onReturnKey = [this] { finishBarOne(true); };
    barOneEditor_.onEscapeKey = [this] { finishBarOne(false); };
    barOneEditor_.onFocusLost = [this] { finishBarOne(true); };
    setToCursor_.setWantsKeyboardFocus(false);
    setToCursor_.onClick = [this] {
        if (onBarOneAtCursor) onBarOneAtCursor();
    };
    clickSound_.setItems(kSoundWords, 0);
    subdivision_.setItems(kSubdivisionWords, 0);
    clickOn_.onChange = [this](bool) { sendClick(); };
    accent_.onChange = [this](bool) { sendClick(); };
    clickSound_.onChange = [this](int) { sendClick(); };
    subdivision_.onChange = [this](int) { sendClick(); };
    for (juce::Component* child : std::initializer_list<juce::Component*>{&songLabel_, &clickLabel_, &barOneEditor_, &setToCursor_,
                                                                          &clickOn_, &accent_, &clickSound_, &subdivision_})
        addAndMakeVisible(child);
}

bool EditorInspector::show(const juce::String& songId, const EditorSelection& picked, TempoLane* tempoLane, bool editable) {
    using Kind = EditorSelection::Kind;
    const bool tempo = picked.kind == Kind::Tempo && tempoLane != nullptr && picked.index >= 0
                       && picked.index < static_cast<int>(tempoLane->state().points.size());
    const bool change = picked.kind == Kind::Marker && findMarker(session_, songId, picked.deviceId, picked.id) != nullptr;
    sectionCard_.show(songId, picked.kind == Kind::Section ? picked.id : juce::String());
    const bool section = sectionCard_.state().visible;
    if ((picked.kind == Kind::Tempo && !tempo) || (picked.kind == Kind::Marker && !change) || (picked.kind == Kind::Section && !section))
        return false;
    tempoCard_.show(tempo ? tempoLane : nullptr, picked.index, editable);
    tempoCard_.setVisible(tempo);
    sectionCard_.setVisible(section);
    jumpCard_.show(songId);
    jumpCard_.setVisible(true);
    markerInspector_.show(songId, change ? picked : EditorSelection{});
    markerView_.setVisible(change);
    // The song panels give the column to the change inspector.
    for (juce::Component* c : std::initializer_list<juce::Component*>{&songLabel_, &clickLabel_, &barOneEditor_, &setToCursor_, &clickOn_,
                                                                      &accent_, &clickSound_, &subdivision_})
        c->setVisible(!change);
    resized();
    repaint();
    return true;
}

bool EditorInspector::changeShown() const { return markerView_.isVisible(); }

void EditorInspector::forgetTempoLane() { tempoCard_.show(nullptr, -1, false); }

void EditorInspector::setSongValues(const SongCardValues& values, bool editable) {
    values_ = values;
    if (!barOneEditor_.hasKeyboardFocus(true)) barOneEditor_.setText(formatSecondsExact(values.barOneSeconds), juce::dontSendNotification);
    barOneEditor_.setReadOnly(!editable);
    setToCursor_.setEnabled(editable);
    clickOn_.setOn(values.clickOn);
    accent_.setOn(values.accentFirstBeat);
    clickSound_.setSelected(values.clickSound);
    subdivision_.setSelected(values.subdivision);
    for (juce::Component* c : std::initializer_list<juce::Component*>{&clickOn_, &accent_, &clickSound_, &subdivision_})
        c->setEnabled(editable);
}

void EditorInspector::finishBarOne(bool keep) {
    if (keep)
        if (const auto seconds = parseTypedNumber(barOneEditor_.getText()); seconds && *seconds != values_.barOneSeconds && onBarOne)
            onBarOne(*seconds);
    barOneEditor_.setText(formatSecondsExact(values_.barOneSeconds), juce::dontSendNotification);
    if (barOneEditor_.hasKeyboardFocus(false) && getParentComponent() != nullptr && getParentComponent()->isShowing())
        getParentComponent()->grabKeyboardFocus();
}

void EditorInspector::sendClick() {
    if (onClick) onClick(clickOn_.isOn(), clickSound_.selected(), subdivision_.selected(), accent_.isOn());
}

void EditorInspector::resized() {
    rowLabels_.clear();
    auto column = getLocalBounds().withTrimmedLeft(1).reduced(kPad);
    const auto card = [&](int height) {
        auto c = column.removeFromTop(std::min(column.getHeight(), height));
        column.removeFromTop(std::min(kCardGap, column.getHeight()));
        return c;
    };
    if (changeShown()) {
        songCard_ = clickCard_ = {};
        const int ideal = markerInspector_.idealHeight();
        const int room = std::max(0, column.getHeight() - jumpCard_.idealHeight() - kCardGap);
        markerView_.setBounds(card(std::min(ideal, room)));
        const int w = std::max(0, column.getWidth() - (ideal > room ? markerView_.getScrollBarThickness() : 0));
        markerInspector_.setSize(w, ideal);
        jumpCard_.setBounds(card(jumpCard_.idealHeight()));
        return;
    }
    const auto rowsCard = [&](int rows) { return card(2 * kCardPadY + kCapsHeight + kCapsGap + rows * kCardRow); };
    if (tempoCard_.isVisible()) tempoCard_.setBounds(card(tempoCard_.idealHeight()));
    if (sectionCard_.isVisible()) sectionCard_.setBounds(card(sectionCard_.idealHeight()));
    const auto rowOf = [this](juce::Rectangle<int>& inner, const juce::String& label, juce::Component* control, int controlWidth,
                              int height) {
        auto r = inner.removeFromTop(std::min(kCardRow, inner.getHeight()));
        if (control != nullptr) {
            const int w = std::min(controlWidth, r.getWidth());
            control->setBounds(r.removeFromRight(w).withSizeKeepingCentre(w, std::min(height, r.getHeight())));
            r.removeFromRight(std::min(kLabelGap, r.getWidth()));
        }
        if (label.isNotEmpty()) rowLabels_.push_back({r, label});
    };
    songCard_ = rowsCard(2);
    auto songInner = songCard_.reduced(kCardPadX, kCardPadY);
    songLabel_.setBounds(songInner.removeFromTop(kCapsHeight));
    songInner.removeFromTop(kCapsGap);
    rowOf(songInner, "Bar 1 starts at", &barOneEditor_, kValueWidth, kFieldHeight);
    rowOf(songInner, {}, &setToCursor_, setToCursor_.idealWidth(), 28);
    clickCard_ = rowsCard(4);
    auto clickInner = clickCard_.reduced(kCardPadX, kCardPadY);
    clickLabel_.setBounds(clickInner.removeFromTop(kCapsHeight));
    clickInner.removeFromTop(kCapsGap);
    rowOf(clickInner, "On", &clickOn_, 34, 18);
    rowOf(clickInner, "Sound", &clickSound_, kChoiceWidth, kFieldHeight);
    rowOf(clickInner, "Subdivision", &subdivision_, kChoiceWidth, kFieldHeight);
    rowOf(clickInner, "Accent the first beat", &accent_, 34, 18);
    jumpCard_.setBounds(card(jumpCard_.idealHeight()));
}

void EditorInspector::paint(juce::Graphics& g) {
    // The column, its border on the left, the SONG and CLICK cards and their row labels (the other cards draw their own).
    g.setColour(juce::Colours::black.withAlpha(0.15f));
    g.fillRect(getLocalBounds());
    g.setColour(theme::hairline);
    g.fillRect(0, 0, 1, getHeight());
    for (const auto& card : {songCard_, clickCard_})
        if (!card.isEmpty()) theme::paintPanel(g, card.toFloat());
    g.setColour(theme::mid);
    g.setFont(bodyFont());
    for (const auto& [area, label] : rowLabels_)
        g.drawText(shorten(label, bodyFont(), static_cast<float>(area.getWidth())), area, juce::Justification::centredLeft, false);
}

}  // namespace sqlive::ui
