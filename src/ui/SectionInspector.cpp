#include "ui/SectionInspector.h"

#include <algorithm>
#include <initializer_list>

#include "model/JsonObject.h"
#include "model/Sections.h"
#include "ui/CuesLane.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The right-hand column's cards (.card, .row), as the TEMPO card has them.
constexpr int kCardPadX = 12, kCardPadY = 10, kCapsHeight = 14, kCapsGap = 6, kCardRow = 30, kFieldHeight = 24;
constexpr int kNameWidth = 150, kChoiceWidth = 150, kBarWidth = 64, kLabelGap = 10;
constexpr int kTwoLineRow = 38, kMinLabel = 56;
constexpr int kAtWidth = 120, kAtHeight = 22;                     // the CUE card's .val: "Bar 7 · beat 1"

const juce::String kNoType = "No type";
const juce::StringArray kTimings{"The end of the section", "The next bar"};

juce::Font bodyFont() { return theme::font(theme::sizeBody); }

int rowsHeight(int rows) { return 2 * kCardPadY + kCapsHeight + kCapsGap + rows * kCardRow; }

// A typed bar number; 0 when it isn't one.
int typedBar(const juce::String& text) {
    const auto trimmed = text.trim();
    return trimmed.isNotEmpty() && trimmed.containsOnly("0123456789") ? trimmed.getIntValue() : 0;
}

void setUpField(juce::TextEditor& field) {
    field.setFont(theme::font(theme::sizeSmall));
    field.setIndents(8, 3);
}

}  // namespace

SectionCardState makeSectionCardState(const app::Session& session, const juce::String& songId, const juce::String& sectionId) {
    SectionCardState state;
    const auto* song = session.model().song(songId);
    if (song == nullptr || sectionId.isEmpty()) return state;
    const auto tiles = model::tileSections(*song, model::barsInSong(*song));
    const auto found = std::find_if(tiles.begin(), tiles.end(), [&](const model::TiledSection& t) { return t.id == sectionId; });
    if (found == tiles.end()) return state;
    state.visible = true;
    state.sectionId = sectionId;
    state.name = found->name;
    for (const auto& type : session.model().band().sectionTypes) {
        if (type.id == found->typeId) state.type = state.types.size();
        state.types.add(type.name);
        state.typeColours.push_back(theme::sectionColour(session.model().band(), type.id));
    }
    if (state.type < 0) state.type = state.types.size();          // none, or one the band no longer has
    state.types.add(kNoType);
    state.typeColours.push_back(juce::Colours::transparentBlack);
    state.startBar = found->startBar;
    const int bars = found->endBar - found->startBar;
    state.length = juce::String(bars) + (bars == 1 ? " bar" : " bars");
    state.first = found == tiles.begin();
    state.last = tiles.size() < 2;
    return state;
}

// ---- The SECTION card --------------------------------------------------------------------------------------------------

SectionCard::SectionCard(app::Session& session) : session_(session) {
    setUpField(name_);
    name_.onReturnKey = [this] { finishName(true); };
    name_.onEscapeKey = [this] { finishName(false); };
    name_.onFocusLost = [this] { finishName(true); };
    setUpField(start_);
    start_.setJustification(juce::Justification::centredRight);
    start_.setInputRestrictions(5, "0123456789");
    start_.onReturnKey = [this] { finishStart(true); };
    start_.onEscapeKey = [this] { finishStart(false); };
    start_.onFocusLost = [this] { finishStart(true); };
    type_.onChange = [this](int item) { setType(item); };
    remove_.onClick = [this] { remove(); };
    remove_.setWantsKeyboardFocus(false);
    for (juce::Component* child : std::initializer_list<juce::Component*>{&label_, &name_, &type_, &start_, &remove_})
        addAndMakeVisible(child);
}

bool SectionCard::editable() const {
    return session_.mode() == app::Mode::Edit && state_.visible && session_.model().song(songId_) != nullptr;
}

void SectionCard::show(const juce::String& songId, const juce::String& sectionId) {
    // Another section: what was typed goes to the one it was for, while the song still has it (an undo may have taken it).
    if (sectionId != state_.sectionId && makeSectionCardState(session_, songId_, state_.sectionId).visible) {
        finishName(true);
        finishStart(true);
    }
    songId_ = songId;
    state_ = makeSectionCardState(session_, songId, sectionId);
    updateControls();
    resized();
    repaint();
}

void SectionCard::updateControls() {
    const bool edit = editable();
    if (!name_.hasKeyboardFocus(false)) name_.setText(state_.name, juce::dontSendNotification);
    if (!start_.hasKeyboardFocus(false)) start_.setText(juce::String(state_.startBar), juce::dontSendNotification);
    name_.setReadOnly(!edit);
    start_.setReadOnly(!edit || state_.first);                    // the first section starts at bar 1
    start_.setEnabled(!state_.first);
    type_.setItems(state_.types, state_.type);
    type_.setDots(state_.typeColours);
    type_.setEnabled(edit);
    remove_.setEnabled(edit && !state_.last);                     // a song keeps at least one section
}

int SectionCard::idealHeight() const { return rowsHeight(5); }

void SectionCard::resized() {
    rowLabels_.clear();
    auto inner = getLocalBounds().reduced(kCardPadX, kCardPadY);
    label_.setBounds(inner.removeFromTop(kCapsHeight));
    inner.removeFromTop(kCapsGap);
    const auto row = [this, &inner](const juce::String& label, juce::Component* control, int width, int height) {
        auto r = inner.removeFromTop(std::min(kCardRow, inner.getHeight()));
        if (control != nullptr) {
            const int w = std::min(width, r.getWidth());
            control->setBounds(r.removeFromRight(w).withSizeKeepingCentre(w, std::min(height, r.getHeight())));
            r.removeFromRight(std::min(kLabelGap, r.getWidth()));
        }
        if (label.isNotEmpty()) rowLabels_.push_back({r, label});
        return r;
    };
    row("Name", &name_, kNameWidth, kFieldHeight);
    row("Type", &type_, kChoiceWidth, kFieldHeight);
    row("Starts at bar", &start_, kBarWidth, kFieldHeight);
    const auto length = row("Length", nullptr, 0, 0);
    lengthArea_ = length;
    row({}, &remove_, remove_.idealWidth(), 28);
}

void SectionCard::paint(juce::Graphics& g) {
    theme::paintPanel(g, getLocalBounds().toFloat());
    g.setFont(bodyFont());
    g.setColour(theme::mid);
    for (const auto& [area, words] : rowLabels_) g.drawText(words, area, juce::Justification::centredLeft, false);
    g.setColour(theme::text);
    g.drawText(state_.length, lengthArea_, juce::Justification::centredRight, false);
}

bool SectionCard::run(const juce::String& command, const juce::var& params) {
    if (!editable()) return false;
    const auto result = session_.commands().run(command, params);
    if (!result.ok) {
        tellUser(message_, *this, result.message);
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<SectionCard>(this)] {   // the fields go back
            if (safe != nullptr) safe->show(safe->songId_, safe->state_.sectionId);
        });
    }
    return result.ok;
}

void SectionCard::setName(const juce::String& name) {
    if (name.trim().isEmpty() || name.trim() == state_.name) return;
    run("section.set", model::jsonObject({{"songId", songId_}, {"sectionId", state_.sectionId}, {"name", name.trim()}}));
    show(songId_, state_.sectionId);
}

void SectionCard::setType(int index) {
    const auto& types = session_.model().band().sectionTypes;
    if (index < 0 || index >= state_.types.size()) return;
    const auto typeId = index < static_cast<int>(types.size()) ? types[static_cast<std::size_t>(index)].id : juce::String();
    run("section.set", model::jsonObject({{"songId", songId_}, {"sectionId", state_.sectionId}, {"typeId", typeId}}));
    show(songId_, state_.sectionId);
}

void SectionCard::setStartBar(int bar) {
    if (bar < 1 || state_.first || bar == state_.startBar) return;
    run("section.set", model::jsonObject({{"songId", songId_}, {"sectionId", state_.sectionId}, {"startBar", bar}}));
    show(songId_, state_.sectionId);
}

void SectionCard::remove() {
    if (!run("section.remove", model::jsonObject({{"songId", songId_}, {"sectionId", state_.sectionId}}))) return;
    if (onRemoved) onRemoved();
}

void SectionCard::finishName(bool keep) {
    if (!name_.isVisible() || name_.isReadOnly()) return;
    const auto text = name_.getText();
    if (keep && text.trim().isNotEmpty() && text.trim() != state_.name) setName(text);
    else name_.setText(state_.name, juce::dontSendNotification);
}

void SectionCard::finishStart(bool keep) {
    if (start_.isReadOnly()) return;
    const int bar = typedBar(start_.getText());
    if (keep && bar > 0 && bar != state_.startBar) setStartBar(bar);
    else start_.setText(juce::String(state_.startBar), juce::dontSendNotification);
}

// ---- The CUE card ------------------------------------------------------------------------------------------------------

CueCardState makeCueCardState(const app::Session& session, const juce::String& songId, const juce::String& cueId) {
    CueCardState state;
    const auto* song = session.model().song(songId);
    if (song == nullptr || cueId.isEmpty()) return state;
    const auto cue = std::find_if(song->cues.begin(), song->cues.end(), [&](const model::CueMarkerDoc& c) { return c.id == cueId; });
    if (cue == song->cues.end()) return state;
    state.visible = true;
    state.cueId = cueId;
    state.at = "Bar " + juce::String(cue->bar) + kDot + "beat " + formatBpm(cue->beat);
    for (const auto& clip : cueClips(session)) {
        state.says.add(clip.words);
        state.clips.add(clip.path);
    }
    state.clip = state.clips.indexOf(cue->clip);
    if (state.clip < 0 && cue->clip.isNotEmpty()) {               // one the band's folder doesn't have (yet)
        state.says.add(cue->text.isNotEmpty() ? cue->text : cue->clip.fromLastOccurrenceOf("/", false, false));
        state.clips.add(cue->clip);
        state.clip = state.clips.size() - 1;
    }
    return state;
}

CueCard::CueCard(app::Session& session) : session_(session), hear_(juce::String::fromUTF8("\xe2\x96\xb6 Hear it")) {
    says_.setPlaceholder("Choose a clip" + kEllipsis);
    says_.onChange = [this](int item) { setClip(item); };
    hear_.onClick = [this] { hear(); };
    hear_.setWantsKeyboardFocus(false);
    for (juce::Component* child : std::initializer_list<juce::Component*>{&label_, &says_, &hear_}) addAndMakeVisible(child);
}

void CueCard::show(const juce::String& songId, const juce::String& cueId) {
    songId_ = songId;
    state_ = makeCueCardState(session_, songId, cueId);
    says_.setItems(state_.says, state_.clip);
    says_.setEnabled(session_.mode() == app::Mode::Edit && state_.visible);
    hear_.setEnabled(state_.clip >= 0 && !session_.now().playing);   // never over a song
    resized();
    repaint();
}

int CueCard::idealHeight() const { return rowsHeight(3); }

void CueCard::setClip(int index) {
    if (!state_.visible || index < 0 || index >= state_.clips.size() || index == state_.clip) return;
    if (session_.mode() != app::Mode::Edit || session_.model().song(songId_) == nullptr) return;
    // The cue says the clip, and its chip reads the clip's words.
    const auto result = session_.commands().run("cue.set", model::jsonObject({{"songId", songId_},
                                                                               {"cueId", state_.cueId},
                                                                               {"clip", state_.clips[index]},
                                                                               {"text", state_.says[index]}}));
    if (!result.ok) tellUser(message_, *this, result.message);
    show(songId_, state_.cueId);
}

bool CueCard::hear() {
    if (!state_.visible || state_.clip < 0) return false;
    return session_.hearCue(state_.clips[state_.clip]);           // false while a song plays, and until the clip has loaded
}

void CueCard::resized() {
    auto inner = getLocalBounds().reduced(kCardPadX, kCardPadY);
    label_.setBounds(inner.removeFromTop(kCapsHeight));
    inner.removeFromTop(kCapsGap);
    const auto row = [&inner](int width, int height, juce::Rectangle<int>& control) {
        auto r = inner.removeFromTop(std::min(kCardRow, inner.getHeight()));
        const int w = std::min(width, r.getWidth());
        control = r.removeFromRight(w).withSizeKeepingCentre(w, std::min(height, r.getHeight()));
        return r.withTrimmedRight(std::min(kLabelGap, r.getWidth()));
    };
    atLabel_ = row(std::min(kAtWidth, widthInPixels(theme::font(theme::sizeBody, true), state_.at) + 18), kAtHeight, atValue_);
    juce::Rectangle<int> place;
    saysLabel_ = row(kChoiceWidth, kFieldHeight, place);
    says_.setBounds(place);
    row(hear_.idealWidth(), 28, place);
    hear_.setBounds(place);
}

void CueCard::paint(juce::Graphics& g) {
    theme::paintPanel(g, getLocalBounds().toFloat());
    g.setFont(bodyFont());
    g.setColour(theme::mid);
    g.drawText("At", atLabel_, juce::Justification::centredLeft, false);
    g.drawText("Says", saysLabel_, juce::Justification::centredLeft, false);
    // The mockup's .val: the place on a field with a hairline border, bright and semibold, on the right.
    const auto value = atValue_.toFloat();
    g.setColour(theme::field);
    g.fillRoundedRectangle(value, theme::radiusField);
    g.setColour(theme::hairline);
    g.drawRoundedRectangle(value.reduced(0.5f), theme::radiusField, 1.0f);
    g.setColour(theme::text);
    g.setFont(theme::font(theme::sizeBody, true));
    g.drawText(state_.at, atValue_.reduced(8, 0), juce::Justification::centredRight, true);
}

// ---- WHEN A SECTION IS CHOSEN LIVE -------------------------------------------------------------------------------------

JumpTimingCard::JumpTimingCard(app::Session& session) : session_(session) {
    timing_.setItems(kTimings, 0);
    timing_.onChange = [this](int item) { setNextBar(item == 1); };
    addAndMakeVisible(label_);
    addAndMakeVisible(timing_);
}

void JumpTimingCard::show(const juce::String& songId) {
    songId_ = songId;
    const auto* song = session_.model().song(songId);
    timing_.setSelected(song != nullptr && song->jumpTiming == model::JumpTiming::NextBar ? 1 : 0);
    timing_.setEnabled(session_.mode() == app::Mode::Edit && song != nullptr);
}

// Its one row is tall enough for its words on two lines, as the mockup wraps them beside the whole choice.
int JumpTimingCard::idealHeight() const { return rowsHeight(0) + kTwoLineRow; }

void JumpTimingCard::setNextBar(bool nextBar) {
    const auto* song = session_.model().song(songId_);
    if (song == nullptr || session_.mode() != app::Mode::Edit) return;
    if ((song->jumpTiming == model::JumpTiming::NextBar) == nextBar) return;
    const auto result = session_.commands().run(
        "song.setJumpTiming", model::jsonObject({{"songId", songId_}, {"timing", nextBar ? "nextBar" : "endOfSection"}}));
    if (!result.ok) tellUser(message_, *this, result.message);
    show(songId_);
}

void JumpTimingCard::resized() {
    auto inner = getLocalBounds().reduced(kCardPadX, kCardPadY);
    label_.setBounds(inner.removeFromTop(kCapsHeight));
    inner.removeFromTop(kCapsGap);
    // The choice is as wide as its words (leaving the label at least kMinLabel); the label wraps when it must.
    auto r = inner.removeFromTop(std::min(kTwoLineRow, inner.getHeight()));
    const int w = std::max(0, std::min(timing_.idealWidth(), r.getWidth() - kMinLabel - kLabelGap));
    timing_.setBounds(r.removeFromRight(w).withSizeKeepingCentre(w, std::min(kFieldHeight, r.getHeight())));
    rowLabel_ = r.withTrimmedRight(std::min(kLabelGap, r.getWidth()));
}

void JumpTimingCard::paint(juce::Graphics& g) {
    theme::paintPanel(g, getLocalBounds().toFloat());
    g.setFont(bodyFont());
    g.setColour(theme::mid);
    g.drawFittedText("It happens at", rowLabel_, juce::Justification::centredLeft, 2, 1.0f);
}

}  // namespace sqlive::ui
