#include "ui/ArrangementRow.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "model/JsonObject.h"
#include "model/Sections.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/SectionsLane.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

const juce::String kInOrder = "In order", kNew = "+ New arrangement";
const juce::String kSeparator = juce::String::fromUTF8("\xe2\x80\xba");   // ›
const juce::String kMore = juce::String::fromUTF8("\xe2\x8b\xaf");        // ⋯
const juce::String kPlayAsArranged = juce::String::fromUTF8("\xe2\x96\xb6 Play as arranged");

// The mockup's .arr: 7 px above and below, 12 px either side, 5 px between things, 12 px words in dim; its chips (span.c)
// 11 px bold in the dark ink, 8 px either side, 2 px above and below, 5 px corners; the length (em) mint and semibold, 6 px
// further off.
constexpr float kPadX = 12.0f, kGap = 5.0f, kLengthGap = 6.0f, kChipPad = 8.0f, kChipHeight = 19.0f, kChipRadius = 5.0f;
constexpr float kChoiceHeight = 22.0f, kMoreWidth = 24.0f, kRightGap = 10.0f;
constexpr float kTightChipPad = 5.0f, kTightGap = 3.0f, kTightestChipPad = 3.0f, kTightestGap = 2.0f;   // when they don't all fit
constexpr float kTextButtonPad = 6.0f;
constexpr int kChoiceMaxWidth = 180, kChoiceRoom = 30, kOutlineMs = 2000, kDragStart = 4;   // kChoiceRoom: the .dd's padding and chevron

juce::Font wordsFont() { return theme::font(theme::sizeSmall); }
juce::Font strongFont() { return theme::font(theme::sizeSmall, true); }
juce::Font chipFont() { return theme::font(theme::sizeCaption, true); }

}  // namespace

ArrangementRowState makeArrangementRowState(const app::Session& session, const juce::String& songId, const juce::String& arrangementId) {
    ArrangementRowState state;
    const auto* song = session.model().song(songId);
    if (song == nullptr) return state;
    state.names.add(kInOrder);
    juce::String chosenId;
    for (const auto& arrangement : song->arrangements) {
        state.names.add(arrangement.name);
        state.arrangementIds.push_back(arrangement.id);
        if (arrangement.id == arrangementId && arrangementId.isNotEmpty()) {
            state.chosen = state.names.size() - 1;
            chosenId = arrangementId;
        }
    }
    state.names.add(kNew);
    const auto tiles = model::tileSections(*song, model::barsInSong(*song));
    const auto& band = session.model().band();
    for (const int i : model::resolveArrangement(*song, chosenId, tiles)) {
        const auto& t = tiles[static_cast<std::size_t>(i)];
        state.chips.push_back({t.id, t.name, theme::sectionColour(band, t.typeId)});
    }
    state.length = formatTime(model::arrangementSeconds(*song, chosenId));
    return state;
}

// ---- The row -----------------------------------------------------------------------------------------------------------

ArrangementRow::ArrangementRow(app::Session& session) : session_(session) {
    choice_.setWantsKeyboardFocus(false);
    choice_.setBold(true);                                        // the mockup's .dd here is font-weight 600
    choice_.onChange = [this](int item) {
        if (item == state_.names.size() - 1) {                    // "+ New arrangement": the one showing stays chosen
            choice_.setSelected(state_.chosen);
            askNewArrangement();
        } else {
            choose(item <= 0 ? juce::String() : state_.arrangementIds[static_cast<std::size_t>(item - 1)]);
        }
    };
    addAndMakeVisible(choice_);
}

bool ArrangementRow::editable() const { return session_.mode() == app::Mode::Edit && session_.model().song(songId_) != nullptr; }

bool ArrangementRow::canPlay() const {
    return onPlayAsArranged != nullptr && session_.mode() == app::Mode::Edit && !session_.now().playing && songId_.isNotEmpty();
}

juce::String ArrangementRow::getTooltip() {
    const auto at = getMouseXYRelative().toFloat();
    if (moreArea_.contains(at)) return "Play as arranged, rename or remove";
    if (playArea_.contains(at)) return "Play the song from its start, as this arrangement plays it";
    return {};
}

void ArrangementRow::show(const juce::String& songId) {
    if (songId != songId_) {
        arrangementId_.clear();
        newChip_ = -1;
    }
    songId_ = songId;
    reread();
}

void ArrangementRow::choose(const juce::String& arrangementId) {
    arrangementId_ = arrangementId;
    newChip_ = -1;
    reread();
}

void ArrangementRow::reread() {
    const auto* song = session_.model().song(songId_);
    const bool has = song != nullptr && std::any_of(song->arrangements.begin(), song->arrangements.end(),
                                                    [this](const model::ArrangementDoc& a) { return a.id == arrangementId_; });
    if (!has) arrangementId_.clear();                             // removed, or undone away: the sections in order
    state_ = makeArrangementRowState(session_, songId_, arrangementId_);
    // The outline is for the chip just put in: once anything else changes the steps (an undo, a redo, another edit), it goes,
    // so it never lands on another chip.
    if (arrangementId_ != newArrangement_ || stepIds() != newSteps_) newChip_ = -1;
    choice_.setItems(state_.names, state_.chosen);
    resized();
    repaint();
    if (arrangementId_ != shownArrangement_) {                    // another one shows: what depends on it follows
        shownArrangement_ = arrangementId_;
        if (onArrangementShown) onArrangementShown();
    }
}

juce::String ArrangementRow::freeName() const {
    const auto* song = session_.model().song(songId_);
    const auto taken = [song](const juce::String& name) {
        return song != nullptr && std::any_of(song->arrangements.begin(), song->arrangements.end(),
                                              [&](const model::ArrangementDoc& a) { return a.name == name; });
    };
    int n = 1;
    while (taken("Arrangement " + juce::String(n))) ++n;
    return "Arrangement " + juce::String(n);
}

std::vector<juce::String> ArrangementRow::stepIds() const {
    std::vector<juce::String> ids;
    for (const auto& chip : state_.chips) ids.push_back(chip.sectionId);
    return ids;
}

// ---- Edits -------------------------------------------------------------------------------------------------------------

bool ArrangementRow::run(const juce::String& command, const juce::var& params, juce::var* value) {
    if (!editable()) return false;
    const auto result = session_.commands().run(command, params);
    if (!result.ok) tellUser(message_, *this, result.message);
    if (value != nullptr) *value = result.value;
    return result.ok;
}

bool ArrangementRow::sendSteps(const std::vector<juce::String>& steps, int newChip) {
    if (!editable() || steps.empty()) return false;
    if (std::any_of(steps.begin(), steps.end(), [](const juce::String& id) { return id.isEmpty(); })) {
        tellUser(message_, *this, "This song has no sections yet.");   // only the whole song, which isn't stored as one
        return false;
    }
    juce::Array<juce::var> list;
    for (const auto& id : steps) list.add(id);
    if (arrangementId_.isEmpty()) {                               // "In order" shows: it becomes a stored arrangement
        juce::var made;
        if (!run("arrangement.add", model::jsonObject({{"songId", songId_}, {"name", freeName()}, {"steps", list}}), &made)) return false;
        arrangementId_ = made.toString();
    } else if (!run("arrangement.set", model::jsonObject({{"songId", songId_}, {"arrangementId", arrangementId_}, {"steps", list}}))) {
        return false;
    }
    newChip_ = newChip;
    newArrangement_ = arrangementId_;
    newSteps_ = steps;
    if (newChip_ >= 0) startTimer(kOutlineMs);
    reread();
    return true;
}

void ArrangementRow::moveChip(int from, int to) {
    reread();
    auto steps = stepIds();
    if (from < 0 || from >= static_cast<int>(steps.size())) return;
    const auto moved = steps[static_cast<std::size_t>(from)];
    steps.erase(steps.begin() + from);
    to = std::clamp(to, 0, static_cast<int>(steps.size()));
    steps.insert(steps.begin() + to, moved);
    if (steps == stepIds()) return;
    sendSteps(steps, -1);
}

void ArrangementRow::removeChip(int index) {
    reread();
    auto steps = stepIds();
    if (index < 0 || index >= static_cast<int>(steps.size()) || steps.size() < 2) return;   // the last chip stays
    steps.erase(steps.begin() + index);
    sendSteps(steps, -1);
}

void ArrangementRow::insertSection(const juce::String& sectionId, int at) {
    reread();
    const auto* song = session_.model().song(songId_);
    if (song == nullptr || sectionId.isEmpty()) return;
    const auto tiles = model::tileSections(*song, model::barsInSong(*song));
    if (std::none_of(tiles.begin(), tiles.end(), [&](const model::TiledSection& t) { return t.id == sectionId; })) return;
    auto steps = stepIds();
    at = std::clamp(at, 0, static_cast<int>(steps.size()));
    steps.insert(steps.begin() + at, sectionId);
    sendSteps(steps, at);
}

void ArrangementRow::newArrangement(const juce::String& name) {
    reread();
    if (!editable() || name.trim().isEmpty()) return;
    auto params = model::jsonObject({{"songId", songId_}, {"name", name.trim()}});
    if (arrangementId_.isNotEmpty()) {                            // a copy of the one showing; else the sections in order
        juce::Array<juce::var> list;
        for (const auto& id : stepIds()) list.add(id);
        params.getDynamicObject()->setProperty("steps", list);
    }
    juce::var made;
    if (run("arrangement.add", params, &made)) choose(made.toString());
}

void ArrangementRow::renameArrangement(const juce::String& name) {
    if (arrangementId_.isEmpty() || name.trim().isEmpty()) return;
    run("arrangement.set", model::jsonObject({{"songId", songId_}, {"arrangementId", arrangementId_}, {"name", name.trim()}}));
    reread();
}

void ArrangementRow::removeArrangement() {
    if (arrangementId_.isEmpty()) return;
    if (run("arrangement.remove", model::jsonObject({{"songId", songId_}, {"arrangementId", arrangementId_}}))) choose({});
}

void ArrangementRow::timerCallback() {
    stopTimer();
    newChip_ = -1;
    repaint();
}

// ---- Menus -------------------------------------------------------------------------------------------------------------

void ArrangementRow::askNewArrangement() {
    if (!editable()) return;
    askUserText(*this, "New arrangement", freeName(), "Add", [this](const juce::String& name) { newArrangement(name); });
}

void ArrangementRow::showMoreMenu() {
    const bool can = editable() && arrangementId_.isNotEmpty();
    juce::PopupMenu menu;
    menu.addItem(3, kPlayAsArranged, canPlay());
    menu.addSeparator();
    menu.addItem(1, "Rename" + kEllipsis, can);
    menu.addItem(2, "Remove", can);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(moreArea_.toNearestInt())),
                       [safe = juce::Component::SafePointer<ArrangementRow>(this)](int result) {
                           if (safe == nullptr) return;
                           if (result == 3 && safe->onPlayAsArranged) safe->onPlayAsArranged();
                           if (result == 2) safe->removeArrangement();
                           if (result != 1) return;
                           const auto name = safe->state_.names[safe->state_.chosen];
                           askUserText(*safe, "Rename the arrangement", name, "Rename",
                                       [safe](const juce::String& typed) { safe->renameArrangement(typed); });
                       });
}

void ArrangementRow::showChipMenu(int chip) {
    if (chip < 0 || chip >= static_cast<int>(chipAreas_.size())) return;
    const bool can = editable();
    juce::PopupMenu menu;
    menu.addItem(1, "Play again after this", can);
    menu.addItem(2, "Take out", can && state_.chips.size() > 1);
    const auto sectionId = state_.chips[static_cast<std::size_t>(chip)].sectionId;
    const auto area = localAreaToGlobal(chipAreas_[static_cast<std::size_t>(chip)].toNearestInt());
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(area),
                       [safe = juce::Component::SafePointer<ArrangementRow>(this), chip, sectionId](int result) {
                           if (safe == nullptr) return;
                           if (result == 1) safe->insertSection(sectionId, chip + 1);
                           else if (result == 2) safe->removeChip(chip);
                       });
}

// ---- Layout and drawing ------------------------------------------------------------------------------------------------

void ArrangementRow::resized() {
    chipAreas_.clear();
    cut_ = false;
    const auto inner = getLocalBounds().toFloat().withTrimmedBottom(1.0f).reduced(kPadX, 0.0f);
    const float cy = inner.getCentreY();
    label_ = lengthArea_ = newArea_ = playArea_ = moreArea_ = {};
    if (songId_.isEmpty() || inner.getWidth() <= 0.0f) {
        choice_.setBounds({});
        return;
    }
    // On the right: "⋯"; before it "▶ Play as arranged" while an arrangement is chosen; before that "+ New arrangement" while
    // there is room for it (the Choice offers it too).
    moreArea_ = {inner.getRight() - kMoreWidth, cy - kChoiceHeight / 2.0f, kMoreWidth, kChoiceHeight};
    float right = moreArea_.getX();
    if (arrangementId_.isNotEmpty()) {
        const float w = widthOf(strongFont(), kPlayAsArranged) + 2.0f * kTextButtonPad;
        playArea_ = {right - kGap - w, cy - kChoiceHeight / 2.0f, w, kChoiceHeight};
        right = playArea_.getX();
    }
    const float newWidth = widthOf(strongFont(), kNew);
    const juce::Rectangle<float> newArea(right - kRightGap - newWidth, inner.getY(), newWidth, inner.getHeight());

    // The chips and the length come first. Tried in turn: with "+ New arrangement"; without it; the chips drawn tighter;
    // without the word "Arrangement"; tighter still; and only then cut, with "…" after the last chip that fits.
    const float lengthWidth = widthOf(strongFont(), state_.length);
    const auto lay = [&](bool label, float pad, float gap, float limit, bool cutAllowed) {
        chipAreas_.clear();
        cut_ = false;
        float x = inner.getX();
        label_ = label ? juce::Rectangle<float>(x, inner.getY(), widthOf(wordsFont(), "Arrangement"), inner.getHeight())
                       : juce::Rectangle<float>();
        if (label) x = label_.getRight() + kGap;
        // As wide as the arrangement showing (the mockup's .dd fits its words).
        const auto shown = state_.names[juce::jlimit(0, std::max(0, state_.names.size() - 1), state_.chosen)];
        const int wanted = widthInPixels(theme::font(theme::sizeBody, true), shown) + kChoiceRoom;
        const int choiceWidth = std::max(0, std::min({wanted, kChoiceMaxWidth, static_cast<int>(limit - x)}));
        const juce::Rectangle<float> choiceArea(x, cy - kChoiceHeight / 2.0f, static_cast<float>(choiceWidth), kChoiceHeight);
        choice_.setBounds(choiceArea.toNearestInt());
        x += static_cast<float>(choiceWidth) + kGap;
        const float separator = gap + widthOf(wordsFont(), kSeparator) + gap;
        const float ellipsis = separator + widthOf(wordsFont(), kEllipsis);
        for (std::size_t i = 0; i < state_.chips.size(); ++i) {
            const float before = i == 0 ? 0.0f : separator;
            const float width = widthOf(chipFont(), state_.chips[i].name) + 2.0f * pad;
            const float after = (i + 1 < state_.chips.size() ? ellipsis : 0.0f) + kLengthGap + kGap + lengthWidth;
            if (x + before + width + after > limit) {
                if (!cutAllowed) return false;
                cut_ = true;
                break;
            }
            x += before;
            chipAreas_.push_back({x, cy - kChipHeight / 2.0f, width, kChipHeight});
            x += width;
        }
        if (cut_) x += ellipsis;
        lengthArea_ = {x + kGap + kLengthGap, inner.getY(), lengthWidth, inner.getHeight()};
        return true;
    };
    const float withNew = newArea.getX() - kRightGap, withoutNew = right - kRightGap;
    newArea_ = newArea;
    if (lay(true, kChipPad, kGap, withNew, false)) return;
    newArea_ = {};
    if (lay(true, kChipPad, kGap, withoutNew, false) || lay(true, kTightChipPad, kTightGap, withoutNew, false)) return;
    if (lay(false, kTightChipPad, kTightGap, withoutNew, false)) return;
    lay(false, kTightestChipPad, kTightestGap, withoutNew, true);
}

void ArrangementRow::paint(juce::Graphics& g) {
    g.setColour(theme::hairline);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
    if (songId_.isEmpty()) return;

    g.setFont(wordsFont());
    g.setColour(theme::dim);
    if (!label_.isEmpty()) g.drawText("Arrangement", label_, juce::Justification::centredLeft, false);
    for (std::size_t i = 0; i < chipAreas_.size(); ++i) {
        const auto area = chipAreas_[i];
        if (i > 0) {                                              // "›" between this chip and the one before
            g.setColour(theme::dim);
            g.setFont(wordsFont());
            g.drawText(kSeparator, juce::Rectangle<float>(chipAreas_[i - 1].getRight(), 0.0f, area.getX() - chipAreas_[i - 1].getRight(),
                                                          static_cast<float>(getHeight() - 1)),
                       juce::Justification::centred, false);
        }
        const auto& chip = state_.chips[i];
        g.setColour(chip.colour);
        g.fillRoundedRectangle(area, kChipRadius);
        g.setColour(theme::onAccent);
        g.setFont(chipFont());
        g.drawText(chip.name, area, juce::Justification::centred, false);
        if (static_cast<int>(i) == newChip_) {                   // .c.new: a 2 px white outline, 1 px out
            g.setColour(juce::Colours::white);
            g.drawRoundedRectangle(area.expanded(2.0f), kChipRadius + 2.0f, 2.0f);
        }
    }
    if (cut_ && !chipAreas_.empty()) {
        g.setColour(theme::dim);
        g.setFont(wordsFont());
        const float x = chipAreas_.back().getRight() + kGap;
        const juce::Rectangle<float> room(x, 0.0f, lengthArea_.getX() - x, static_cast<float>(getHeight() - 1));
        g.drawText(kSeparator + " " + kEllipsis, room,
                   juce::Justification::centredLeft, false);
    }
    if (gap_ >= 0 && !chipAreas_.empty()) {                       // where the chip carried would go
        const float x = gap_ < static_cast<int>(chipAreas_.size()) ? chipAreas_[static_cast<std::size_t>(gap_)].getX() - 3.0f
                                                                   : chipAreas_.back().getRight() + 3.0f;
        g.setColour(theme::text);                                // neutral: mint means "now"
        g.fillRect(juce::Rectangle<float>(x - 1.0f, chipAreas_.front().getY() - 2.0f, 2.0f, kChipHeight + 4.0f));
    }

    g.setColour(theme::mint);
    g.setFont(strongFont());
    g.drawText(state_.length, lengthArea_, juce::Justification::centredLeft, false);
    g.setColour(editable() ? (hoverNew_ ? theme::text : theme::mint) : theme::mint.withAlpha(0.4f));
    g.drawText(kNew, newArea_, juce::Justification::centredLeft, false);
    if (!playArea_.isEmpty()) {                                   // a text button, in the page's own ink (mint means "now")
        if (hoverPlay_ && canPlay()) {
            g.setColour(theme::fieldHover);
            g.fillRoundedRectangle(playArea_, theme::radiusField);
        }
        g.setColour(canPlay() ? theme::text : theme::text.withAlpha(0.4f));
        g.drawText(kPlayAsArranged, playArea_, juce::Justification::centred, false);
    }

    if (hoverMore_) {
        g.setColour(theme::fieldHover);
        g.fillRoundedRectangle(moreArea_, theme::radiusField);
    }
    g.setColour(theme::mid);
    g.setFont(theme::font(theme::sizeBase, true));
    g.drawText(kMore, moreArea_, juce::Justification::centred, false);
}

// ---- The mouse ---------------------------------------------------------------------------------------------------------

int ArrangementRow::chipAt(juce::Point<float> at) const {
    for (std::size_t i = 0; i < chipAreas_.size(); ++i)
        if (chipAreas_[i].expanded(2.0f, 4.0f).contains(at)) return static_cast<int>(i);
    return -1;
}

int ArrangementRow::gapAt(float x) const {
    int gap = 0;
    for (const auto& area : chipAreas_)
        if (area.getCentreX() < x) ++gap;
    return gap;
}

void ArrangementRow::mouseMove(const juce::MouseEvent& e) {
    const bool overNew = newArea_.contains(e.position), overMore = moreArea_.contains(e.position);
    const bool overPlay = playArea_.contains(e.position), overChip = editable() && chipAt(e.position) >= 0;
    const bool pointing = overNew || overMore || overPlay || overChip;
    setMouseCursor(pointing ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (overNew == hoverNew_ && overMore == hoverMore_ && overPlay == hoverPlay_) return;
    hoverNew_ = overNew;
    hoverMore_ = overMore;
    hoverPlay_ = overPlay;
    repaint();
}

void ArrangementRow::mouseExit(const juce::MouseEvent&) {
    if (!hoverNew_ && !hoverMore_ && !hoverPlay_) return;
    hoverNew_ = hoverMore_ = hoverPlay_ = false;
    repaint();
}

void ArrangementRow::mouseDown(const juce::MouseEvent& e) {
    pressed_ = gap_ = -1;
    if (newArea_.contains(e.position) && !e.mods.isPopupMenu()) {
        askNewArrangement();
        return;
    }
    if (moreArea_.contains(e.position)) {
        showMoreMenu();
        return;
    }
    if (playArea_.contains(e.position)) {
        if (!e.mods.isPopupMenu() && canPlay()) onPlayAsArranged();
        return;
    }
    const int chip = chipAt(e.position);
    if (chip < 0) return;
    if (e.mods.isPopupMenu()) showChipMenu(chip);
    else pressed_ = chip;
}

void ArrangementRow::mouseDrag(const juce::MouseEvent& e) {
    if (pressed_ < 0 || !editable() || e.getDistanceFromDragStart() < kDragStart) return;
    gap_ = gapAt(e.position.x);
    repaint();
}

void ArrangementRow::mouseUp(const juce::MouseEvent&) {
    const int from = std::exchange(pressed_, -1), gap = std::exchange(gap_, -1);
    repaint();
    if (from >= 0 && gap >= 0) moveChip(from, gap > from ? gap - 1 : gap);
}

bool ArrangementRow::isInterestedInDragSource(const SourceDetails& details) {
    return editable() && details.description.toString().startsWith(kSectionDragPrefix);
}

void ArrangementRow::itemDragEnter(const SourceDetails& details) { itemDragMove(details); }

void ArrangementRow::itemDragMove(const SourceDetails& details) {
    const int gap = gapAt(static_cast<float>(details.localPosition.x));
    if (gap == gap_) return;
    gap_ = gap;
    repaint();
}

void ArrangementRow::itemDragExit(const SourceDetails&) {
    gap_ = -1;
    repaint();
}

void ArrangementRow::itemDropped(const SourceDetails& details) {
    const int gap = gapAt(static_cast<float>(details.localPosition.x));
    gap_ = -1;
    repaint();
    insertSection(details.description.toString().fromFirstOccurrenceOf(kSectionDragPrefix, false, false), gap);
}

}  // namespace sqlive::ui
