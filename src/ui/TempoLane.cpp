#include "ui/TempoLane.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "model/DocumentsJson.h"
#include "model/JsonObject.h"
#include "model/SongAudio.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's .tempo: 4 px from the top, 20 px high, 4 px corners, 6 px either side of 11 px bold text.
constexpr float kPlateTop = 4.0f, kPlateHeight = 20.0f, kPlatePad = 6.0f, kPlateRadius = 4.0f;
constexpr int kEditorWidth = 96;

juce::Font plateFont() { return theme::font(theme::sizeCaption, true); }

bool samePlace(const model::TempoPointDoc& a, const model::TempoPointDoc& b) { return a.bar == b.bar && a.beat == b.beat; }

// "92.5", "128 · 4/4", "6/8": the BPM and the signature typed, each 0 when not given.
void parseTyped(juce::String text, double& bpm, int& numerator, int& denominator) {
    bpm = 0.0;
    numerator = denominator = 0;
    auto words = juce::StringArray::fromTokens(text.replace(kMiddleDot, " "), " ", "");
    for (const auto& word : words) {
        if (word.containsChar('/')) {
            numerator = word.upToFirstOccurrenceOf("/", false, false).getIntValue();
            denominator = word.fromFirstOccurrenceOf("/", false, false).getIntValue();
        } else if (word.containsAnyOf("0123456789") && word.containsOnly("0123456789.")) {
            bpm = word.getDoubleValue();
        }
    }
}

}  // namespace

TempoLaneState makeTempoLaneState(const app::Session& session, const juce::String& songId, const EditorSelection& selection) {
    TempoLaneState state;
    const auto* song = session.model().song(songId);
    if (song == nullptr) return state;
    const auto map = model::tempoMapOf(*song);
    for (int i = 0; i < static_cast<int>(song->tempo.size()); ++i) {
        const auto& p = song->tempo[static_cast<std::size_t>(i)];
        // The signature in force there: a point off a bar line keeps the one before it.
        const auto text = formatBpm(p.bpm) + kDot + juce::String(map.beatsInBar(p.bar)) + "/" + juce::String(map.denominatorAt(p.bar));
        state.points.push_back({i, p.bar, p.beat, text, selection.kind == EditorSelection::Kind::Tempo && selection.index == i});
    }
    return state;
}

// ---- The lane ----------------------------------------------------------------------------------------------------------

TempoLane::TempoLane(app::Session& session, juce::String songId) : session_(session), songId_(std::move(songId)) {
    editor_.setFont(theme::font(theme::sizeCaption, true));
    editor_.setIndents(6, 3);
    editor_.setJustification(juce::Justification::centredLeft);
    editor_.onReturnKey = [this] { finishEdit(true); };
    editor_.onEscapeKey = [this] { finishEdit(false); };
    editor_.onFocusLost = [this] { finishEdit(true); };
    addChildComponent(editor_);
    refresh();
}

bool TempoLane::editable() const { return session_.mode() == app::Mode::Edit && session_.model().song(songId_) != nullptr; }

void TempoLane::refresh() {
    state_ = makeTempoLaneState(session_, songId_, selection());
    if (editing_ >= static_cast<int>(state_.points.size())) finishEdit(false);   // undone away while it was edited
    repaint();
}

void TempoLane::contextChanged() { state_ = makeTempoLaneState(session_, songId_, selection()); }

bool TempoLane::send(std::vector<model::TempoPointDoc> points, int moved) {
    if (!editable() || points.empty()) return false;
    std::vector<int> order(points.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const auto &pa = points[static_cast<std::size_t>(a)], &pb = points[static_cast<std::size_t>(b)];
        return pa.bar != pb.bar ? pa.bar < pb.bar : pa.beat < pb.beat;
    });
    model::SongDoc doc;
    doc.tempo.clear();
    int movedTo = -1;
    for (const int i : order) {
        const auto& p = points[static_cast<std::size_t>(i)];
        if (!doc.tempo.empty() && samePlace(doc.tempo.back(), p)) return false;   // two points in one place
        if (i == moved) movedTo = static_cast<int>(doc.tempo.size());
        doc.tempo.push_back(p);
    }
    if (doc.tempo.front().bar != 1 || doc.tempo.front().beat != 1.0) return false;   // the first point stays at bar 1
    const auto tempo = model::toVar(doc).getProperty("tempo", juce::var());
    if (!run(session_, "song.setTempo", model::jsonObject({{"songId", songId_}, {"tempo", tempo}}))) return false;
    if (movedTo >= 0 && onSelect) onSelect({EditorSelection::Kind::Tempo, {}, {}, movedTo});
    refresh();
    return true;
}

void TempoLane::addAt(engine::BarBeat at) {
    const auto* song = session_.model().song(songId_);
    if (song == nullptr || !editable() || at.bar < 1) return;
    for (int i = 0; i < static_cast<int>(song->tempo.size()); ++i)
        if (song->tempo[static_cast<std::size_t>(i)].bar == at.bar && song->tempo[static_cast<std::size_t>(i)].beat == at.beat) {
            if (onSelect) onSelect({EditorSelection::Kind::Tempo, {}, {}, i});   // already a point there
            return;
        }
    const auto map = model::tempoMapOf(*song);
    model::TempoPointDoc point;
    point.bar = at.bar;
    point.beat = at.beat;
    point.bpm = map.bpmAt(at);                                    // the tempo already in force
    point.numerator = map.beatsInBar(at.bar);
    point.denominator = map.denominatorAt(at.bar);
    auto points = song->tempo;
    points.push_back(point);
    send(std::move(points), static_cast<int>(song->tempo.size()));
}

void TempoLane::movePoint(int index, engine::BarBeat at) {
    const auto* song = session_.model().song(songId_);
    if (song == nullptr || index <= 0 || index >= static_cast<int>(song->tempo.size()) || at.bar < 1) return;
    auto points = song->tempo;
    auto& point = points[static_cast<std::size_t>(index)];
    if (point.bar == at.bar && point.beat == at.beat) return;
    point.bar = at.bar;
    point.beat = at.beat;
    if (at.beat != 1.0) {                                         // off a bar line: the signature in force there, not its own
        auto others = *song;
        others.tempo.erase(others.tempo.begin() + index);
        const auto map = model::tempoMapOf(others);
        point.numerator = map.beatsInBar(at.bar);
        point.denominator = map.denominatorAt(at.bar);
    }
    send(std::move(points), index);
}

void TempoLane::removePoint(int index) {
    const auto* song = session_.model().song(songId_);
    if (song == nullptr || index <= 0 || index >= static_cast<int>(song->tempo.size())) return;
    auto points = song->tempo;
    points.erase(points.begin() + index);
    if (send(std::move(points), -1) && selection().kind == EditorSelection::Kind::Tempo && onSelect) onSelect({});
}

void TempoLane::setPoint(int index, double bpm, int numerator, int denominator) {
    const auto* song = session_.model().song(songId_);
    if (song == nullptr || index < 0 || index >= static_cast<int>(song->tempo.size())) return;
    auto points = song->tempo;
    auto& point = points[static_cast<std::size_t>(index)];
    if (std::isfinite(bpm) && bpm > 0.0) point.bpm = bpm;
    if (point.beat == 1.0 && numerator > 0 && denominator > 0) {   // off a bar line the signature before it holds
        point.numerator = numerator;
        point.denominator = denominator;
    }
    if (point == song->tempo[static_cast<std::size_t>(index)]) return;
    send(std::move(points), index);
}

bool TempoLane::removeSelected() {
    const auto& picked = selection();
    if (picked.kind != EditorSelection::Kind::Tempo || picked.index <= 0 || picked.index >= static_cast<int>(state_.points.size()))
        return false;
    const auto before = state_.points.size();
    removePoint(picked.index);
    return state_.points.size() < before;
}

juce::Rectangle<float> TempoLane::plateOf(int bar, double beat, const juce::String& text) const {
    const float x = scale() != nullptr ? std::round(scale()->xAtBar(bar, beat)) : 0.0f;
    return {x, kPlateTop, widthOf(plateFont(), text) + 2.0f * kPlatePad, kPlateHeight};
}

std::optional<int> TempoLane::plateAt(juce::Point<float> at) const {
    if (scale() == nullptr) return std::nullopt;
    for (auto p = state_.points.rbegin(); p != state_.points.rend(); ++p)   // the later one is drawn on top
        if (plateOf(p->bar, p->beat, p->text).contains(at)) return p->index;
    return std::nullopt;
}

void TempoLane::paint(juce::Graphics& g) {
    if (scale() == nullptr) return;
    g.setFont(plateFont());
    for (const auto& p : state_.points) {
        const bool dragged = p.index == dragging_ && dragTo_.has_value();
        const auto plate = plateOf(dragged ? dragTo_->bar : p.bar, dragged ? dragTo_->beat : p.beat, p.text);
        if (plate.getRight() < 0.0f || plate.getX() > static_cast<float>(getWidth())) continue;
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.fillRoundedRectangle(plate, kPlateRadius);
        if (p.selected) {                                         // the mockup's .picked: a 2 px white outline, 1 px out
            g.setColour(juce::Colours::white);
            g.drawRoundedRectangle(plate.expanded(2.0f), kPlateRadius + 2.0f, 2.0f);
        }
        g.setColour(theme::text);
        g.drawText(p.text, plate, juce::Justification::centred, false);
    }
}

void TempoLane::resized() {
    if (editing_ >= 0 && editing_ < static_cast<int>(state_.points.size())) {
        const auto& p = state_.points[static_cast<std::size_t>(editing_)];
        const auto plate = plateOf(p.bar, p.beat, p.text);
        editor_.setBounds(plate.withWidth(std::max(plate.getWidth(), static_cast<float>(kEditorWidth))).toNearestInt());
    }
}

void TempoLane::mouseDown(const juce::MouseEvent& e) {
    dragging_ = -1;
    dragTo_.reset();
    emptyPress_ = false;
    if (e.mods.isPopupMenu()) return;
    if (const auto hit = plateAt(e.position)) {
        if (onSelect) onSelect({EditorSelection::Kind::Tempo, {}, {}, *hit});
        if (*hit > 0 && editable()) dragging_ = *hit;             // the first point can't move
        return;
    }
    if (tool() == EditTool::Draw && editable() && scale() != nullptr) {   // in Show mode the press moves the cursor
        addAt(snapped(e.position.x, e.mods));
        return;
    }
    emptyPress_ = true;
    pointer(e, true);                                             // empty space: the cursor, and nothing selected
}

void TempoLane::mouseDrag(const juce::MouseEvent& e) {
    if (dragging_ >= 0) {
        if (!dragTo_ && e.getDistanceFromDragStart() < 3) return;
        dragTo_ = snapped(e.position.x, e.mods);
        repaint();
    } else if (emptyPress_ && !e.mods.isPopupMenu()) {
        pointer(e, false);
    }
}

void TempoLane::mouseUp(const juce::MouseEvent&) {
    const int index = std::exchange(dragging_, -1);
    const auto to = std::exchange(dragTo_, std::nullopt);
    emptyPress_ = false;
    if (index >= 0 && to) movePoint(index, *to);
    repaint();
}

void TempoLane::mouseDoubleClick(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) return;
    if (const auto hit = plateAt(e.position)) editInPlace(*hit);
    else if (tool() == EditTool::Select && editable() && scale() != nullptr) addAt(snapped(e.position.x, e.mods));
}

void TempoLane::editInPlace(int index) {
    if (!editable() || index < 0 || index >= static_cast<int>(state_.points.size())) return;
    editing_ = index;
    editor_.setText(state_.points[static_cast<std::size_t>(index)].text, juce::dontSendNotification);
    resized();
    editor_.setVisible(true);
    if (editor_.isShowing()) editor_.grabKeyboardFocus();
    editor_.selectAll();
}

void TempoLane::finishEdit(bool keep) {
    if (editing_ < 0) return;
    const int index = std::exchange(editing_, -1);
    const auto text = editor_.getText();
    editor_.setVisible(false);
    if (!keep) return;
    double bpm = 0.0;
    int numerator = 0, denominator = 0;
    parseTyped(text, bpm, numerator, denominator);
    setPoint(index, bpm, numerator, denominator);
}

// ---- The TEMPO card ----------------------------------------------------------------------------------------------------

namespace {
constexpr int kCardPadX = 12, kCardPadY = 10, kCapsHeight = 14, kCardRow = 30, kFieldHeight = 24, kChoiceWidth = 130;
}  // namespace

TempoCard::TempoCard() {
    bpm_.setFont(theme::font(theme::sizeSmall));
    bpm_.setIndents(8, 3);
    bpm_.setJustification(juce::Justification::centredRight);
    bpm_.setInputRestrictions(7, "0123456789.");
    bpm_.onReturnKey = [this] { finishBpm(); };
    bpm_.onFocusLost = [this] { finishBpm(); };
    bpm_.onEscapeKey = [this] {                                    // back to what the point has
        if (lane_ != nullptr && index_ < static_cast<int>(lane_->state().points.size()))
            bpm_.setText(lane_->state().points[static_cast<std::size_t>(index_)].text.upToFirstOccurrenceOf(" ", false, false),
                         juce::dontSendNotification);
    };
    signature_.onChange = [this](int item) {
        if (lane_ == nullptr || item < 0 || item >= signature_.items().size()) return;
        const auto& signature = signature_.items()[item];
        lane_->setPoint(index_, 0.0, signature.upToFirstOccurrenceOf("/", false, false).getIntValue(),
                        signature.fromFirstOccurrenceOf("/", false, false).getIntValue());
    };
    remove_.onClick = [this] {
        if (lane_ != nullptr) lane_->removePoint(index_);
    };
    remove_.setWantsKeyboardFocus(false);
    for (juce::Component* child : std::initializer_list<juce::Component*>{&label_, &bpm_, &signature_, &remove_}) addAndMakeVisible(child);
}

void TempoCard::show(TempoLane* lane, int index, bool editable) {
    lane_ = lane;
    index_ = index;
    if (lane_ == nullptr || index < 0 || index >= static_cast<int>(lane_->state().points.size())) {
        lane_ = nullptr;
        return;
    }
    const auto& point = lane_->state().points[static_cast<std::size_t>(index)];
    if (!bpm_.hasKeyboardFocus(false)) bpm_.setText(point.text.upToFirstOccurrenceOf(" ", false, false), juce::dontSendNotification);
    bpm_.setReadOnly(!editable);
    onBarLine_ = point.beat == 1.0;
    auto signatures = kTimeSignatures;
    const auto current = point.text.fromLastOccurrenceOf(" ", false, false);
    if (!signatures.contains(current)) signatures.add(current);   // one from a MIDI file, say 7/4
    signature_.setItems(signatures, signatures.indexOf(current));
    signature_.setVisible(onBarLine_);
    signature_.setEnabled(editable);
    remove_.setEnabled(editable && index > 0);                    // the first point can't be removed
    resized();
    repaint();
}

int TempoCard::idealHeight() const { return 2 * kCardPadY + kCapsHeight + 6 + (onBarLine_ ? 3 : 2) * kCardRow; }

void TempoCard::resized() {
    auto inner = getLocalBounds().reduced(kCardPadX, kCardPadY);
    label_.setBounds(inner.removeFromTop(kCapsHeight));
    inner.removeFromTop(6);
    const auto row = [&inner](juce::Component& control, int width, int height) {
        auto r = inner.removeFromTop(std::min(kCardRow, inner.getHeight()));
        const int w = std::min(width, r.getWidth());
        control.setBounds(r.removeFromRight(w).withSizeKeepingCentre(w, std::min(height, r.getHeight())));
        return r.withTrimmedRight(std::min(10, r.getWidth()));
    };
    bpmLabel_ = row(bpm_, 96, kFieldHeight);
    signatureLabel_ = onBarLine_ ? row(signature_, kChoiceWidth, kFieldHeight) : juce::Rectangle<int>();
    row(remove_, remove_.idealWidth(), 28);
}

void TempoCard::paint(juce::Graphics& g) {
    theme::paintPanel(g, getLocalBounds().toFloat());
    g.setColour(theme::mid);
    g.setFont(theme::font(theme::sizeBody));
    g.drawText("BPM", bpmLabel_, juce::Justification::centredLeft, false);
    if (!signatureLabel_.isEmpty()) g.drawText("Signature", signatureLabel_, juce::Justification::centredLeft, false);
}

void TempoCard::finishBpm() {
    if (lane_ == nullptr || bpm_.isReadOnly()) return;
    const auto text = bpm_.getText().trim();
    if (text.containsAnyOf("0123456789") && text.containsOnly("0123456789.")) lane_->setPoint(index_, text.getDoubleValue(), 0, 0);
}

}  // namespace sqlive::ui
