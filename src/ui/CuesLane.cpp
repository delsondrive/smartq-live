#include "ui/CuesLane.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>
#include <utility>

#include "app/VoiceMaker.h"
#include "model/JsonObject.h"
#include "model/Sections.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using model::jsonObject;

namespace {

// The mockup's .mk in a lane: 4 px from the top, 16 px high, 4 px corners, 5 px either side of 10 px bold text.
constexpr float kChipTop = 4.0f, kChipHeight = 16.0f, kChipPad = 5.0f, kChipRadius = 4.0f, kTick = 6.0f, kChipGap = 1.0f;
const float kDashes[] = {3.0f, 2.0f};                             // .mk.auto's 1 px dashed border

juce::Font chipFont() { return theme::font(theme::sizeTag, true); }

const juce::String kOpenQuote = juce::String::fromUTF8("\xe2\x80\x9c"), kCloseQuote = juce::String::fromUTF8("\xe2\x80\x9d");

juce::String leadWords(model::CueLead lead) {
    switch (lead) {
        case model::CueLead::TwoBeats: return "2 beats early";
        case model::CueLead::TwoBars: return "2 bars early";
        case model::CueLead::OneBar: break;
    }
    return "1 bar early";
}

// A clip's words from its path: the file's name without its extension ("cues/Count in.wav" says "Count in").
juce::String wordsOf(const juce::String& path) {
    return path.fromLastOccurrenceOf("/", false, false).upToLastOccurrenceOf(".", false, false);
}

}  // namespace

CuesLaneState makeCuesLaneState(const app::Session& session, const juce::String& songId, const EditorSelection& selection) {
    CuesLaneState state;
    const auto& band = session.model().band();
    state.subtitle = leadWords(band.cueLead);
    const auto* song = session.model().song(songId);
    if (song == nullptr) return state;
    const auto tiles = model::tileSections(*song, model::barsInSong(*song));
    for (std::size_t i = 0; i + 1 < tiles.size(); ++i) {
        const auto& next = tiles[i + 1];
        const auto type = std::find_if(band.sectionTypes.begin(), band.sectionTypes.end(),
                                       [&](const model::SectionTypeDoc& t) { return next.typeId.isNotEmpty() && t.id == next.typeId; });
        if (type == band.sectionTypes.end() || type->cueClip.isEmpty()) continue;
        const auto at = model::announcePlace(*song, tiles[i], band.cueLead);
        state.chips.push_back({next.id, type->name, at.bar, at.beat, true, false});
    }
    for (const auto& cue : song->cues) {
        const auto words = cue.text.isNotEmpty() ? kOpenQuote + cue.text + kCloseQuote : wordsOf(cue.clip);
        const bool selected = selection.kind == EditorSelection::Kind::Cue && selection.id == cue.id;
        state.chips.push_back({cue.id, words, cue.bar, cue.beat, false, selected});
    }
    std::stable_sort(state.chips.begin(), state.chips.end(), [](const auto& a, const auto& b) {
        return std::make_tuple(a.bar, a.beat, !a.automatic) < std::make_tuple(b.bar, b.beat, !b.automatic);
    });
    return state;
}

std::vector<CueClip> cueClips(const app::Session& session) {
    std::vector<CueClip> clips;
    std::set<juce::String> listed;
    for (const auto& phrase : app::VoiceMaker::extraPhrases()) {
        clips.push_back({"cues/built-in/" + phrase + ".wav", phrase});
        listed.insert(clips.back().path);
    }
    for (const auto& path : session.cues().clipsInFolder())
        if (listed.insert(path).second) clips.push_back({path, wordsOf(path)});
    return clips;
}

// ---- The lane ----------------------------------------------------------------------------------------------------------

CuesLane::CuesLane(app::Session& session, juce::String songId) : session_(session), songId_(std::move(songId)) { refresh(); }

bool CuesLane::editable() const { return session_.mode() == app::Mode::Edit && session_.model().song(songId_) != nullptr; }

bool CuesLane::has(const juce::String& cueId) const {
    const auto* song = session_.model().song(songId_);
    return song != nullptr && cueId.isNotEmpty()
           && std::any_of(song->cues.begin(), song->cues.end(), [&](const model::CueMarkerDoc& c) { return c.id == cueId; });
}

void CuesLane::refresh() {
    state_ = makeCuesLaneState(session_, songId_, selection());
    layoutChips();
    repaint();
}

void CuesLane::contextChanged() {
    state_ = makeCuesLaneState(session_, songId_, selection());
    layoutChips();
}

engine::BarBeat CuesLane::placeAt(float x, juce::ModifierKeys mods) const {
    if (scale() == nullptr) return {};
    auto at = snapped(x, mods);                                   // Alt: no snap
    if (at.bar < 1) at = {1, 1.0};                                // a cue can't be in the count-in
    return at;
}

// ---- Edits -------------------------------------------------------------------------------------------------------------

juce::String CuesLane::addAt(engine::BarBeat at, const juce::String& clip, const juce::String& text) {
    if (!editable()) return {};
    if (at.bar < 1) at = {1, 1.0};
    juce::var id;
    const auto params = jsonObject({{"songId", songId_}, {"bar", at.bar}, {"beat", at.beat}, {"clip", clip}, {"text", text}});
    if (!run(session_, "cue.add", params, {}, &id)) return {};
    if (onSelect) onSelect({EditorSelection::Kind::Cue, {}, id.toString(), -1});
    refresh();
    return id.toString();
}

void CuesLane::moveCue(const juce::String& cueId, engine::BarBeat at) {
    if (!editable() || !has(cueId)) return;
    if (at.bar < 1) at = {1, 1.0};
    const auto& cues = session_.model().song(songId_)->cues;
    const auto cue = std::find_if(cues.begin(), cues.end(), [&](const model::CueMarkerDoc& c) { return c.id == cueId; });
    if (cue->bar == at.bar && cue->beat == at.beat) return;       // dropped where it was: no command
    run(session_, "cue.set", jsonObject({{"songId", songId_}, {"cueId", cueId}, {"bar", at.bar}, {"beat", at.beat}}));
    refresh();
}

void CuesLane::removeCue(const juce::String& cueId) {
    if (!editable() || !has(cueId)) return;
    if (!run(session_, "cue.remove", jsonObject({{"songId", songId_}, {"cueId", cueId}}))) return;
    const auto picked = selection();
    if (picked.kind == EditorSelection::Kind::Cue && picked.id == cueId && onSelect) onSelect({});
    refresh();
}

bool CuesLane::removeSelected() {
    const auto picked = selection();                              // a copy: removing changes the selection
    if (picked.kind != EditorSelection::Kind::Cue || !has(picked.id) || !editable()) return false;
    removeCue(picked.id);
    return !has(picked.id);
}

void CuesLane::chooseClipAt(engine::BarBeat at) {
    const auto clips = cueClips(session_);
    const int phrases = app::VoiceMaker::extraPhrases().size();
    juce::PopupMenu menu;
    for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
        if (i == phrases) menu.addSeparator();                    // the phrases, then the band's own files
        menu.addItem(i + 1, clips[static_cast<std::size_t>(i)].words);
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                       [safe = juce::Component::SafePointer<CuesLane>(this), clips, at](int result) {
                           if (safe == nullptr || result <= 0 || result > static_cast<int>(clips.size())) return;
                           const auto& clip = clips[static_cast<std::size_t>(result) - 1];
                           safe->addAt(at, clip.path, clip.words);
                       });
}

// ---- Drawing -----------------------------------------------------------------------------------------------------------

void CuesLane::layoutChips() {
    areas_.clear();
    if (scale() == nullptr) return;
    const auto font = chipFont();
    for (const auto& chip : state_.chips) {
        const float x = std::round(scale()->xAtBar(chip.bar, chip.beat));
        areas_.push_back({{x, kChipTop, widthOf(font, chip.label) + 2.0f * kChipPad, kChipHeight}, false});
    }
    // Each chip ends before the next one starts, down to a tick.
    for (std::size_t i = 0; i + 1 < areas_.size(); ++i) {
        auto& chip = areas_[i];
        const float room = areas_[i + 1].area.getX() - chip.area.getX() - kChipGap;
        if (room < chip.area.getWidth()) {
            chip.area.setWidth(std::max(kTick, room));
            chip.narrowed = true;
        }
    }
}

std::optional<int> CuesLane::chipAt(juce::Point<float> at) const {
    for (int i = static_cast<int>(areas_.size()) - 1; i >= 0; --i)   // the later one is drawn on top
        if (areas_[static_cast<std::size_t>(i)].area.contains(at)) return i;
    return std::nullopt;
}

juce::String CuesLane::getTooltip() {
    const auto hit = chipAt(getMouseXYRelative().toFloat());
    return hit && areas_[static_cast<std::size_t>(*hit)].narrowed ? state_.chips[static_cast<std::size_t>(*hit)].label : juce::String();
}

void CuesLane::paintChip(juce::Graphics& g, juce::Rectangle<float> area, const CuesLaneState::Chip& chip, bool selected) const {
    const float radius = std::min(kChipRadius, area.getWidth() / 2.0f);
    if (chip.automatic) {                                         // .mk.auto: no fill, a dashed dim border, dim words
        juce::Path outline, dashed;
        outline.addRoundedRectangle(area.reduced(0.5f), radius);
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, kDashes, 2);
        g.setColour(theme::dim);
        g.fillPath(dashed);
    } else {
        if (selected)                                             // .picked: a white glow and a 2 px outline, 1 px out
            juce::DropShadow(juce::Colours::white.withAlpha(0.5f), 12, {}).drawForRectangle(g, area.toNearestInt());
        g.setColour(theme::mid);
        g.fillRoundedRectangle(area, radius);
        if (selected) {
            g.setColour(juce::Colours::white);
            g.drawRoundedRectangle(area.expanded(2.0f), radius + 2.0f, 2.0f);
        }
    }
    const auto words = shorten(chip.label, chipFont(), area.getWidth() - 2.0f * kChipPad);
    if (words.isEmpty()) return;
    g.setColour(chip.automatic ? theme::dim : theme::onAccent);
    g.setFont(chipFont());
    g.drawText(words, area.reduced(kChipPad, 0.0f), juce::Justification::centredLeft, false);
}

void CuesLane::paint(juce::Graphics& g) {
    if (scale() == nullptr) return;
    const float width = static_cast<float>(getWidth());
    for (std::size_t i = 0; i < areas_.size() && i < state_.chips.size(); ++i) {
        const auto& chip = state_.chips[i];
        if (!chip.automatic && chip.id == dragging_ && dragTo_) continue;   // drawn where it is dragged to
        const auto& area = areas_[i].area;
        if (area.getRight() < -4.0f || area.getX() > width + 4.0f) continue;
        paintChip(g, area, chip, chip.selected);
    }
    if (!dragTo_) return;
    const auto dragged = std::find_if(state_.chips.begin(), state_.chips.end(),
                                      [&](const auto& c) { return !c.automatic && c.id == dragging_; });
    if (dragged == state_.chips.end()) return;
    const float x = std::round(scale()->xAtBar(dragTo_->bar, dragTo_->beat));
    paintChip(g, {x, kChipTop, widthOf(chipFont(), dragged->label) + 2.0f * kChipPad, kChipHeight}, *dragged, true);
}

// ---- Gestures ----------------------------------------------------------------------------------------------------------

void CuesLane::mouseDown(const juce::MouseEvent& e) {
    dragging_.clear();
    dragTo_.reset();
    emptyPress_ = false;
    if (e.mods.isPopupMenu()) return;
    if (const auto hit = chipAt(e.position)) {
        // Copied first: selecting rebuilds state_ and the chips (the timeline gives every lane its context again).
        const auto chip = state_.chips[static_cast<std::size_t>(*hit)];
        const float x = areas_[static_cast<std::size_t>(*hit)].area.getX();
        if (chip.automatic) {                                     // it can't move: it follows its section
            if (onSelect) onSelect({EditorSelection::Kind::Section, {}, chip.id, -1});
            return;
        }
        if (onSelect) onSelect({EditorSelection::Kind::Cue, {}, chip.id, -1});
        if (editable()) {
            dragging_ = chip.id;
            dragFromX_ = x;
        }
        return;
    }
    if (tool() == EditTool::Draw && editable() && scale() != nullptr) {
        chooseClipAt(placeAt(e.position.x, e.mods));
        return;
    }
    emptyPress_ = true;
    pointer(e, true);                                             // empty space: the cursor, and nothing selected
}

void CuesLane::mouseDrag(const juce::MouseEvent& e) {
    if (dragging_.isNotEmpty()) {
        if ((!dragTo_ && e.getDistanceFromDragStart() < 3) || scale() == nullptr) return;
        const float x = juce::jlimit(0.0f, scale()->width(), dragFromX_ + e.position.x - e.mouseDownPosition.x);
        dragTo_ = placeAt(x, e.mods);
        repaint();
    } else if (emptyPress_ && !e.mods.isPopupMenu()) {
        pointer(e, false);
    }
}

void CuesLane::mouseUp(const juce::MouseEvent&) {
    const auto id = std::exchange(dragging_, {});
    const auto to = std::exchange(dragTo_, std::nullopt);
    emptyPress_ = false;
    if (id.isNotEmpty() && to) moveCue(id, *to);                  // one cue.set: one undo step
    repaint();
}

void CuesLane::mouseDoubleClick(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu() || chipAt(e.position)) return;
    if (tool() == EditTool::Select && editable() && scale() != nullptr) chooseClipAt(placeAt(e.position.x, e.mods));
}

}  // namespace sqlive::ui
