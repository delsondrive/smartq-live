#include "ui/AutomationOverlay.h"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

#include "model/DocumentsJson.h"
#include "model/JsonObject.h"
#include "model/Units.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's automation line: white, 1.5 px, its points 3.5 px round; with the other tools, 1 px and a little faded.
constexpr float kLine = 1.5f, kQuietLine = 1.0f, kQuietAlpha = 0.7f, kPointRadius = 3.5f, kGrab = 6.0f;
constexpr float kPickedRadius = 5.5f;                             // the selected point's white ring, with a soft glow

bool before(engine::BarBeat a, engine::BarBeat b) { return std::tie(a.bar, a.beat) < std::tie(b.bar, b.beat); }

const EditorSelection& none() {
    static const EditorSelection nothing;
    return nothing;
}

}  // namespace

AutomationState makeAutomationState(const app::Session& session, const juce::String& songId, const juce::String& target,
                                    const EditorSelection& selection) {
    AutomationState state;
    state.target = target;
    const auto* song = session.model().song(songId);
    if (song == nullptr) return state;
    const auto found = song->automation.find(target);
    if (found == song->automation.end()) return state;
    const bool here = selection.kind == EditorSelection::Kind::Automation && selection.id == target;
    for (int i = 0; i < static_cast<int>(found->second.size()); ++i) {
        const auto& p = found->second[static_cast<std::size_t>(i)];
        state.points.push_back({i, p.bar, p.beat, p.db, here && selection.index == i});
    }
    std::stable_sort(state.points.begin(), state.points.end(), [](const auto& a, const auto& b) {
        return std::tie(a.bar, a.beat) < std::tie(b.bar, b.beat);
    });
    return state;
}

std::vector<engine::AutoPoint> automationCurve(const AutomationState& state, const engine::TempoMap& tempo) {
    std::vector<engine::AutoPoint> curve;
    for (const auto& p : state.points)
        curve.push_back({static_cast<engine::SamplePos>(std::llround(tempo.secondsAt({p.bar, p.beat}) * 1000.0)), p.db});
    return curve;
}

float automationGainAt(const std::vector<engine::AutoPoint>& curve, double seconds) {
    if (curve.empty()) return 1.0f;
    return engine::automationGain(curve, static_cast<engine::SamplePos>(std::llround(seconds * 1000.0)));
}

// ---- The overlay -------------------------------------------------------------------------------------------------------

AutomationOverlay::AutomationOverlay(app::Session& session, juce::String songId, juce::String target)
    : session_(session), songId_(std::move(songId)), target_(std::move(target)) {
    update();
}

float AutomationOverlay::dbAtY(float y, float height) {
    if (!(height > 0.0f)) return topDb;
    return juce::jlimit(bottomDb, topDb, topDb - (topDb - bottomDb) * y / height);
}

float AutomationOverlay::yForDb(float db, float height) {
    return (topDb - juce::jlimit(bottomDb, topDb, db)) / (topDb - bottomDb) * height;
}

bool AutomationOverlay::editable() const { return session_.mode() == app::Mode::Edit && session_.model().song(songId_) != nullptr; }

bool AutomationOverlay::editing() const { return tool_ == EditTool::Automation && editable(); }

void AutomationOverlay::setContext(const BarScale* scale, EditTool tool, SnapTo snap, const EditorSelection* selection) {
    scale_ = scale;
    tool_ = tool;
    snap_ = snap;
    selection_ = selection;
    update();
}

void AutomationOverlay::refresh() { update(); }

void AutomationOverlay::update() {
    state_ = makeAutomationState(session_, songId_, target_, selection_ != nullptr ? *selection_ : none());
    setVisible(hasPoints() || tool_ == EditTool::Automation);
    setInterceptsMouseClicks(editing(), false);                   // with the other tools a press is the lane's
    repaint();
}

// ---- Edits -------------------------------------------------------------------------------------------------------------

bool AutomationOverlay::setPoints(const std::vector<model::AutomationPoint>& points) {
    if (!editable()) return false;
    juce::Array<juce::var> list;
    for (const auto& p : points) list.add(model::toVar(p));
    const auto params = model::jsonObject({{"songId", songId_}, {"target", target_}, {"points", list}});
    const auto result = session_.commands().run("automation.set", params);
    if (!result.ok) tellUser(message_, *this, result.message);
    update();
    return result.ok;
}

int AutomationOverlay::addPoint(engine::BarBeat at, float db) {
    const auto* song = session_.model().song(songId_);
    if (song == nullptr || !editable()) return -1;
    if (at.bar < 1) at = {1, 1.0};                                // never in the count-in
    const auto found = song->automation.find(target_);
    auto points = found != song->automation.end() ? found->second : std::vector<model::AutomationPoint>();
    model::AutomationPoint point;
    point.bar = at.bar;
    point.beat = at.beat;
    point.db = juce::jlimit(bottomDb, topDb, db);
    points.push_back(point);
    if (!setPoints(points)) return -1;
    // Stored in time order: the new point is the last at its place with its level.
    int added = -1;
    {
        const auto& stored = session_.model().song(songId_)->automation.at(target_);
        for (int i = static_cast<int>(stored.size()) - 1; i >= 0 && added < 0; --i) {
            const auto& p = stored[static_cast<std::size_t>(i)];
            if (p.bar == point.bar && p.beat == point.beat && p.db == point.db) added = i;
        }
    }
    if (added >= 0 && onSelect) onSelect({EditorSelection::Kind::Automation, {}, target_, added});
    return added;
}

engine::BarBeat AutomationOverlay::between(int shown, engine::BarBeat at) const {
    if (shown > 0) {
        const auto& p = state_.points[static_cast<std::size_t>(shown) - 1];
        if (before(at, {p.bar, p.beat})) at = {p.bar, p.beat};
    }
    if (shown + 1 < static_cast<int>(state_.points.size())) {
        const auto& n = state_.points[static_cast<std::size_t>(shown) + 1];
        if (before({n.bar, n.beat}, at)) at = {n.bar, n.beat};
    }
    return at;
}

void AutomationOverlay::movePoint(int index, engine::BarBeat at, float db) {
    update();                                                     // the neighbours as the song has them now
    const auto* song = session_.model().song(songId_);
    if (song == nullptr || !editable()) return;
    const auto found = song->automation.find(target_);
    if (found == song->automation.end() || index < 0 || index >= static_cast<int>(found->second.size())) return;
    const auto shownAt = std::find_if(state_.points.begin(), state_.points.end(), [index](const auto& p) { return p.index == index; });
    const int shown = static_cast<int>(shownAt - state_.points.begin());
    if (at.bar < 1) at = {1, 1.0};
    at = between(shown, at);
    auto points = found->second;
    auto& point = points[static_cast<std::size_t>(index)];
    db = juce::jlimit(bottomDb, topDb, db);
    if (point.bar == at.bar && point.beat == at.beat && point.db == db) return;
    point.bar = at.bar;
    point.beat = at.beat;
    point.db = db;
    const auto& picked = selection_ != nullptr ? *selection_ : none();
    const bool wasPicked = picked.kind == EditorSelection::Kind::Automation && picked.id == target_ && picked.index == index;
    // Kept between its neighbours, it keeps its place in time order, which is where automation.set stores it.
    if (setPoints(points) && wasPicked && shown != index && onSelect) onSelect({EditorSelection::Kind::Automation, {}, target_, shown});
}

void AutomationOverlay::removePoint(int index) {
    const auto* song = session_.model().song(songId_);
    if (song == nullptr || !editable()) return;
    const auto found = song->automation.find(target_);
    if (found == song->automation.end() || index < 0 || index >= static_cast<int>(found->second.size())) return;
    auto points = found->second;
    const bool sorted = std::is_sorted(points.begin(), points.end(), [](const auto& a, const auto& b) {
        return std::tie(a.bar, a.beat) < std::tie(b.bar, b.beat);
    });
    points.erase(points.begin() + index);
    const auto picked = selection_ != nullptr ? *selection_ : none();
    if (!setPoints(points)) return;
    if (picked.kind != EditorSelection::Kind::Automation || picked.id != target_ || !onSelect) return;
    if (picked.index == index || !sorted) onSelect({});           // it went, or the list was put in order: nothing selected
    else if (picked.index > index) onSelect({EditorSelection::Kind::Automation, {}, target_, picked.index - 1});
}

bool AutomationOverlay::removeSelected() {
    const auto& picked = selection_ != nullptr ? *selection_ : none();
    if (picked.kind != EditorSelection::Kind::Automation || picked.id != target_ || !editable()) return false;
    const auto here = std::any_of(state_.points.begin(), state_.points.end(), [&](const auto& p) { return p.index == picked.index; });
    if (!here) return false;
    removePoint(picked.index);
    return true;
}

// ---- Drawing -----------------------------------------------------------------------------------------------------------

juce::Point<float> AutomationOverlay::where(int bar, double beat, float db) const {
    if (scale_ == nullptr) return {};
    return {scale_->xAtBar(bar, beat), yForDb(db, static_cast<float>(getHeight()))};
}

std::optional<int> AutomationOverlay::pointAt(juce::Point<float> at) const {
    std::optional<int> nearest;
    float best = kGrab;
    for (int i = 0; i < static_cast<int>(state_.points.size()); ++i) {
        const auto& p = state_.points[static_cast<std::size_t>(i)];
        const float d = where(p.bar, p.beat, p.db).getDistanceFrom(at);
        if (d <= best) {                                          // the later of two on one spot: the one drawn on top
            best = d;
            nearest = i;
        }
    }
    return nearest;
}

engine::BarBeat AutomationOverlay::placeAt(float x, juce::ModifierKeys mods) const {
    if (scale_ == nullptr) return {};
    auto at = mods.isAltDown() ? scale_->tempo().barBeatAt(scale_->secondsAtX(x)) : snapAt(*scale_, x, snap_);
    if (at.bar < 1) at = {1, 1.0};
    return at;
}

void AutomationOverlay::paint(juce::Graphics& g) {
    if (scale_ == nullptr || state_.points.empty()) return;
    const bool edit = tool_ == EditTool::Automation;
    std::vector<juce::Point<float>> spots;
    for (const auto& p : state_.points) {
        if (dragTo_ && p.index == pressed_) spots.push_back(where(dragTo_->at.bar, dragTo_->at.beat, dragTo_->db));
        else spots.push_back(where(p.bar, p.beat, p.db));
    }
    // Flat before the first point and after the last, as the engine plays it.
    juce::Path line;
    line.startNewSubPath(0.0f, spots.front().y);
    for (const auto& spot : spots) line.lineTo(spot);
    line.lineTo(static_cast<float>(getWidth()), spots.back().y);
    g.setColour(juce::Colours::white.withAlpha(edit ? 1.0f : kQuietAlpha));
    g.strokePath(line, juce::PathStrokeType(edit ? kLine : kQuietLine, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (!edit) return;
    for (std::size_t i = 0; i < spots.size(); ++i) {
        const auto& p = state_.points[i];
        const auto spot = spots[i];
        if (p.selected || (dragTo_ && p.index == pressed_)) {     // the mockup's .picked: a white glow and ring
            g.setColour(juce::Colours::white.withAlpha(0.25f));
            g.fillEllipse(juce::Rectangle<float>(2.0f * (kPickedRadius + 3.0f), 2.0f * (kPickedRadius + 3.0f)).withCentre(spot));
            g.setColour(juce::Colours::white);
            g.drawEllipse(juce::Rectangle<float>(2.0f * kPickedRadius, 2.0f * kPickedRadius).withCentre(spot), 1.5f);
        }
        g.setColour(juce::Colours::white);
        g.fillEllipse(juce::Rectangle<float>(2.0f * kPointRadius, 2.0f * kPointRadius).withCentre(spot));
    }
}

// ---- Gestures ----------------------------------------------------------------------------------------------------------

void AutomationOverlay::mouseDown(const juce::MouseEvent& e) {
    pressed_ = -1;
    dragTo_.reset();
    if (e.getNumberOfClicks() <= 1) added_ = -1;                  // a click's first press: nothing it added yet
    if (e.mods.isPopupMenu() || !editing() || scale_ == nullptr) return;
    if (const auto hit = pointAt(e.position)) {
        // Copied first: selecting rebuilds state_ (the timeline gives every lane its context again).
        const auto p = state_.points[static_cast<std::size_t>(*hit)];
        pressedAt_ = where(p.bar, p.beat, p.db);
        if (onSelect) onSelect({EditorSelection::Kind::Automation, {}, target_, p.index});
        pressed_ = p.index;
        return;
    }
    added_ = addPoint(placeAt(e.position.x, e.mods), toTenth(dbAtY(e.position.y, static_cast<float>(getHeight()))));
}

void AutomationOverlay::mouseDrag(const juce::MouseEvent& e) {
    if (pressed_ < 0 || scale_ == nullptr || (!dragTo_ && e.getDistanceFromDragStart() < 3)) return;
    const auto shownAt = std::find_if(state_.points.begin(), state_.points.end(), [this](const auto& p) { return p.index == pressed_; });
    if (shownAt == state_.points.end()) return;
    const auto moved = pressedAt_ + (e.position - e.mouseDownPosition);
    const auto at = between(static_cast<int>(shownAt - state_.points.begin()),
                            placeAt(juce::jlimit(0.0f, scale_->width(), moved.x), e.mods));
    dragTo_ = Drag{at, toTenth(dbAtY(moved.y, static_cast<float>(getHeight())))};
    repaint();
}

void AutomationOverlay::mouseUp(const juce::MouseEvent&) {
    const int index = std::exchange(pressed_, -1);
    const auto to = std::exchange(dragTo_, std::nullopt);
    if (index >= 0 && to) movePoint(index, to->at, to->db);       // one automation.set: one undo step
    repaint();
}

void AutomationOverlay::mouseDoubleClick(const juce::MouseEvent& e) {
    const int added = std::exchange(added_, -1);
    if (e.mods.isPopupMenu() || !editing()) return;
    if (const auto hit = pointAt(e.position)) {
        const int index = state_.points[static_cast<std::size_t>(*hit)].index;
        if (index == added) return;                               // a double-click on empty space adds one point, no more
        pressed_ = -1;
        dragTo_.reset();
        removePoint(index);
    }
}

}  // namespace sqlive::ui
