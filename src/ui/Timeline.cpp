#include "ui/Timeline.h"

#include <algorithm>
#include <cmath>

#include "model/SongAudio.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

constexpr float kMinBarGap = 6.0f, kMinBeatGap = 12.0f;          // pixels between grid lines

bool sameTempo(const engine::TempoMap& a, const engine::TempoMap& b) {
    const auto &pa = a.points(), &pb = b.points();
    return std::equal(pa.begin(), pa.end(), pb.begin(), pb.end(), [](const engine::TempoPoint& x, const engine::TempoPoint& y) {
        return x.bar == y.bar && x.beat == y.beat && x.bpm == y.bpm && x.numerator == y.numerator && x.denominator == y.denominator;
    });
}

bool sameScale(const BarScale& a, const BarScale& b) {
    return a.startSeconds() == b.startSeconds() && a.endSeconds() == b.endSeconds() && a.width() == b.width()
           && sameTempo(a.tempo(), b.tempo());
}

}  // namespace

engine::BarBeat snapAt(const BarScale& scale, float x, SnapTo snap) {
    if (snap == SnapTo::Bar) return scale.snap(x, 0);
    if (snap == SnapTo::Beat) return scale.snap(x, 1);
    const int bar = scale.tempo().barBeatAt(scale.secondsAtX(x)).bar;
    return scale.snap(x, std::max(1, 8 / scale.tempo().denominatorAt(bar)));
}

// ---- A lane ------------------------------------------------------------------------------------------------------------

void Lane::setContext(const BarScale* scale, EditTool tool, SnapTo snap, const EditorSelection* selection) {
    scale_ = scale;
    tool_ = tool;
    snap_ = snap;
    selection_ = selection;
    const bool changed = scale == nullptr ? seen_.has_value() : !seen_ || !sameScale(*seen_, *scale);
    if (changed) {
        if (scale != nullptr) seen_.emplace(*scale);
        else seen_.reset();
        scaleChanged();
    }
    contextChanged();
    repaint();
}

const EditorSelection& Lane::selection() const {
    static const EditorSelection none;
    return selection_ != nullptr ? *selection_ : none;
}

engine::BarBeat Lane::snapped(float x) const { return scale_ != nullptr ? snapAt(*scale_, x, snap_) : engine::BarBeat{}; }

void Lane::mouseDown(const juce::MouseEvent& e) {
    pressedEmpty_ = !e.mods.isPopupMenu();
    if (pressedEmpty_) pointer(e, true);
}

void Lane::mouseDrag(const juce::MouseEvent& e) {
    if (pressedEmpty_ && !e.mods.isPopupMenu()) pointer(e, false);
}

void Lane::pointer(const juce::MouseEvent& e, bool press) {
    if (onPointer) onPointer(e, press);
}

bool Lane::run(app::Session& session, const juce::String& command, const juce::var& params, const juce::String& coalesceKey) {
    if (session.mode() != app::Mode::Edit) return false;
    const auto result = session.commands().run(command, params, coalesceKey);
    if (!result.ok) tellUser(message_, *this, result.message);
    return result.ok;
}

// ---- The lanes and their headers, which scroll -------------------------------------------------------------------------

// The viewed component: each lane's header in the column on the left, and the grid behind the lanes. A press on a header
// is the lane's; a press on the lane area that no lane covers moves the cursor.
class Timeline::Body : public juce::Component {
public:
    explicit Body(Timeline& timeline) : timeline_(timeline) {}

    int idealHeight() const {
        int h = 0;
        for (const auto& lane : timeline_.lanes_) h += lane->laneHeight();
        return h;
    }

    // The grid, in lane x: bars every step (more than one when bars are narrow), beats when they are wide enough.
    void setGrid(const BarScale* scale) {
        bars_.clear();
        beats_.clear();
        if (scale != nullptr && scale->width() > 0.0f) {
            const auto& tempo = scale->tempo();
            const int first = tempo.barBeatAt(scale->startSeconds()).bar, last = tempo.barBeatAt(scale->endSeconds()).bar;
            const float perBar = scale->width() / static_cast<float>(std::max(1, last - first + 1));
            int step = 1;
            while (perBar * static_cast<float>(step) < kMinBarGap) step *= 2;
            for (int bar = first; bar <= last; ++bar) {
                if (((bar - 1) % step + step) % step != 0) continue;
                bars_.push_back(std::round(scale->xAtBar(bar)));
                if (step != 1) continue;
                const int beats = tempo.beatsInBar(bar);
                if (beats > 1 && (scale->xAtBar(bar + 1) - scale->xAtBar(bar)) / static_cast<float>(beats) >= kMinBeatGap)
                    for (int beat = 2; beat <= beats; ++beat) beats_.push_back(std::round(scale->xAtBar(bar, beat)));
            }
        }
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto clip = g.getClipBounds();
        const float left = static_cast<float>(headerWidth);
        for (const auto& lane : timeline_.lanes_) {
            const juce::Rectangle<int> row(0, lane->getY(), getWidth(), lane->getHeight());
            if (!row.intersects(clip)) continue;
            paintHeader(g, row.withWidth(std::min(headerWidth, getWidth())), *lane);
            const float top = static_cast<float>(row.getY()), height = static_cast<float>(row.getHeight() - 1);
            g.setColour(juce::Colours::white.withAlpha(0.025f));
            for (const float x : beats_) g.fillRect(left + x, top, 1.0f, height);
            g.setColour(juce::Colours::white.withAlpha(0.05f));
            for (const float x : bars_) g.fillRect(left + x, top, 1.0f, height);
            g.setColour(theme::hairline);
            g.fillRect(headerWidth, row.getBottom() - 1, std::max(0, getWidth() - headerWidth), 1);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        pressedLanes_ = e.x >= headerWidth;
        if (e.mods.isPopupMenu()) return;
        if (pressedLanes_) {
            timeline_.pointerAt(e.position.x - static_cast<float>(headerWidth), e.mods, true, true);
            return;
        }
        for (const auto& lane : timeline_.lanes_)
            if (e.y >= lane->getY() && e.y < lane->getBottom()) {
                lane->headerPressed(e);
                return;
            }
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (pressedLanes_ && !e.mods.isPopupMenu())
            timeline_.pointerAt(e.position.x - static_cast<float>(headerWidth), e.mods, true, false);
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
        timeline_.mouseWheelMove(e.getEventRelativeTo(&timeline_), wheel);
    }

private:
    // The mockup's .hd: a darker cell with its dot, its title in 12 px semibold and its subtitle on the right in dim.
    static void paintHeader(juce::Graphics& g, juce::Rectangle<int> head, const Lane& lane) {
        g.setColour(juce::Colours::black.withAlpha(0.18f));
        g.fillRect(head);
        g.setColour(theme::hairline);
        g.fillRect(head.getRight() - 1, head.getY(), 1, head.getHeight());
        g.fillRect(head.getX(), head.getBottom() - 1, head.getWidth(), 1);
        auto text = head.toFloat().withTrimmedRight(1.0f).reduced(8.0f, 0.0f);
        if (const auto dot = lane.dot(); !dot.isTransparent()) {
            g.setColour(dot);
            g.fillEllipse(text.getX(), text.getCentreY() - 4.0f, 8.0f, 8.0f);
            text.removeFromLeft(14.0f);
        }
        const auto titleFont = theme::font(theme::sizeSmall, true), subtitleFont = theme::font(theme::sizeSmall);
        if (const auto subtitle = lane.subtitle(); subtitle.isNotEmpty()) {
            const float w = std::min(text.getWidth() / 2.0f, widthOf(subtitleFont, subtitle) + 1.0f);
            g.setColour(theme::dim);
            g.setFont(subtitleFont);
            g.drawText(shorten(subtitle, subtitleFont, w), text.removeFromRight(w), juce::Justification::centredRight, false);
            text.removeFromRight(6.0f);
        }
        g.setColour(theme::mid);
        g.setFont(titleFont);
        g.drawText(shorten(lane.title(), titleFont, std::max(0.0f, text.getWidth())), text, juce::Justification::centredLeft, false);
    }

    Timeline& timeline_;
    std::vector<float> bars_, beats_;
    bool pressedLanes_ = false;
};

// ---- The cursor --------------------------------------------------------------------------------------------------------

// The white line, with a soft glow, over the ruler and the lanes. Never takes the mouse.
class Timeline::CursorLine : public juce::Component {
public:
    CursorLine() { setInterceptsMouseClicks(false, false); }

    void setX(std::optional<float> x) {
        if (x == x_) return;
        repaintAround(x_);
        x_ = x;
        repaintAround(x_);
    }

    void paint(juce::Graphics& g) override {
        if (!x_) return;
        const float h = static_cast<float>(getHeight());
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.fillRect(*x_ - 5.0f, 0.0f, 10.0f, h);
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.fillRect(*x_ - 3.0f, 0.0f, 6.0f, h);
        g.setColour(juce::Colours::white);
        g.fillRect(*x_ - 1.0f, 0.0f, 2.0f, h);
    }

private:
    void repaintAround(std::optional<float> x) {
        if (x) repaint(juce::roundToInt(*x) - 7, 0, 14, getHeight());
    }

    std::optional<float> x_;
};

// ---- The timeline ------------------------------------------------------------------------------------------------------

Timeline::Timeline(app::Session& session)
    : session_(session), body_(std::make_unique<Body>(*this)), cursorLine_(std::make_unique<CursorLine>()) {
    view_.setViewedComponent(body_.get(), false);
    view_.setScrollBarsShown(true, false);
    view_.setScrollBarThickness(scrollBarThickness);
    addAndMakeVisible(view_);
    ruler_.onPointer = [this](float x, juce::ModifierKeys mods) { pointerAt(x, mods, false, true); };
    addAndMakeVisible(ruler_);
    across_.setAutoHide(false);
    across_.addListener(this);
    addChildComponent(across_);
    addAndMakeVisible(*cursorLine_);                              // last: over the ruler and the lanes
}

Timeline::~Timeline() { across_.removeListener(this); }

void Timeline::setSong(const juce::String& songId) {
    songId_ = songId;
    zoom_ = 1.0;
    selection_ = {};
    tempo_.reset();
    readSong();
    cursor_ = tempo_ ? start_ : 0.0;
    viewStart_ = start_;
    resized();                                                    // the scroll bar goes with zoom 1
}

void Timeline::setLanes(std::vector<std::unique_ptr<Lane>> lanes) {
    for (auto& lane : lanes_) body_->removeChildComponent(lane.get());
    lanes_ = std::move(lanes);
    for (auto& lane : lanes_) {
        auto* l = lane.get();
        l->onSelect = [this](EditorSelection s) { select(std::move(s)); };
        l->onPointer = [this, l](const juce::MouseEvent& e, bool press) {
            pointerAt(e.getEventRelativeTo(l).position.x, e.mods, true, press);
        };
        body_->addAndMakeVisible(*l);
        l->refresh();
    }
    resized();
}

bool Timeline::readSong() {
    const auto* song = session_.model().song(songId_);
    if (song == nullptr) {
        const bool changed = tempo_.has_value();
        tempo_.reset();
        start_ = viewStart_ = 0.0;
        end_ = 1.0;
        return changed;
    }
    auto tempo = model::tempoMapOf(*song);
    auto [start, end] = model::songExtentSeconds(*song, tempo);
    if (!(end > start)) end = start + 1.0;
    if (tempo_ && sameTempo(*tempo_, tempo) && start == start_ && end == end_) return false;
    tempo_ = std::move(tempo);
    start_ = start;
    end_ = end;
    return true;
}

void Timeline::refresh() {
    if (readSong()) {
        setViewStart(viewStart_);
        rebuildScale();
    }
    for (auto& lane : lanes_) lane->refresh();
    followPlayhead();
}

void Timeline::followPlayhead() {
    if (!tempo_) return;
    const auto now = session_.now();
    if (!now.playing || now.playingSongId != songId_ || !std::isfinite(now.seconds)) return;
    cursor_ = now.seconds;
    if (scale_ && zoom_ > 1.0) {                                  // the view turns the page when the playhead leaves it
        const float x = scale_->xAtSeconds(cursor_);
        if (x < 0.0f || x > scale_->width() * 0.95f) {
            setViewStart(cursor_ - visibleSeconds() * 0.05);
            rebuildScale();
        }
    }
    moveCursorLine();
}

void Timeline::setTool(EditTool tool) {
    tool_ = tool;
    giveContext();
}

void Timeline::setSnap(SnapTo snap) {
    snap_ = snap;
    giveContext();
}

void Timeline::setCursorSeconds(double seconds) {
    if (!std::isfinite(seconds)) return;
    cursor_ = tempo_ ? juce::jlimit(start_, end_, seconds) : seconds;
    moveCursorLine();
}

void Timeline::setZoom(double zoom) {
    if (std::isnan(zoom)) return;
    zoom = juce::jlimit(1.0, maxZoom, zoom);
    if (zoom == zoom_) return;
    // About the cursor: it stays where it is on screen, or, out of view, comes to the middle.
    const double visible = visibleSeconds();
    double at = visible > 0.0 ? (cursor_ - viewStart_) / visible : 0.5;
    if (!(at >= 0.0 && at <= 1.0)) at = 0.5;
    const bool across = zoom_ > 1.0;
    zoom_ = zoom;
    setViewStart(cursor_ - at * visibleSeconds());
    if ((zoom_ > 1.0) != across) resized();                       // the scroll bar comes or goes
    else rebuildScale();
}

void Timeline::select(EditorSelection selection) {
    if (selection == selection_) return;
    selection_ = std::move(selection);
    giveContext();
    if (onSelectionChanged) onSelectionChanged();
}

bool Timeline::removeSelected() {
    if (selection_.kind == EditorSelection::Kind::None) return false;
    for (auto& lane : lanes_)
        if (lane->removeSelected()) {
            select({});
            return true;
        }
    return false;
}

void Timeline::setViewStart(double seconds) {
    viewStart_ = std::isfinite(seconds) ? juce::jlimit(start_, std::max(start_, end_ - visibleSeconds()), seconds) : start_;
    across_.setRangeLimits(start_, end_, juce::dontSendNotification);
    across_.setCurrentRange(viewStart_, visibleSeconds(), juce::dontSendNotification);
    across_.setSingleStepSize(visibleSeconds() * 0.05);
}

void Timeline::scrollBarMoved(juce::ScrollBar*, double newRangeStart) {
    setViewStart(newRangeStart);
    rebuildScale();
}

void Timeline::pointerAt(float x, juce::ModifierKeys mods, bool fromLane, bool press) {
    if (press && onPressed) onPressed();
    // The song is looked up every time: an undo can take it away before the editor has heard.
    if (!scale_ || session_.model().song(songId_) == nullptr) return;
    if (fromLane && press && tool_ == EditTool::Select) select({});
    const auto now = session_.now();
    if (now.playing && now.playingSongId == songId_) return;     // while it plays, the cursor follows the playhead
    x = juce::jlimit(0.0f, scale_->width(), x);
    setCursorSeconds(mods.isAltDown() ? scale_->secondsAtX(x) : scale_->tempo().secondsAt(snapAt(*scale_, x, snap_)));
    if (onCursorMoved) onCursorMoved();
}

void Timeline::rebuildScale() {
    const int laneWidth = ruler_.getWidth();
    if (!tempo_ || laneWidth <= 0) scale_.reset();
    else scale_.emplace(*tempo_, viewStart_, viewStart_ + visibleSeconds(), static_cast<float>(laneWidth));
    if (scale_) ruler_.setScale(*scale_);
    body_->setGrid(scale_ ? &*scale_ : nullptr);
    giveContext();
    moveCursorLine();
}

void Timeline::giveContext() {
    for (auto& lane : lanes_) lane->setContext(scale_ ? &*scale_ : nullptr, tool_, snap_, &selection_);
}

void Timeline::moveCursorLine() {
    if (!scale_) {
        cursorLine_->setX(std::nullopt);
        return;
    }
    const float x = scale_->xAtSeconds(cursor_);
    cursorLine_->setX(x >= 0.0f && x <= scale_->width() ? std::optional<float>(x) : std::nullopt);
}

void Timeline::layoutLanes() {
    int y = 0;
    for (auto& lane : lanes_) {
        lane->setBounds(headerWidth, y, std::max(0, body_->getWidth() - headerWidth), lane->laneHeight());
        y += lane->laneHeight();
    }
}

void Timeline::resized() {
    auto area = getLocalBounds();
    barsHead_ = area.removeFromTop(std::min(BarRuler::height, area.getHeight())).withWidth(std::min(headerWidth, area.getWidth()));
    const bool across = zoom_ > 1.0 && tempo_.has_value();
    across_.setVisible(across);
    const auto bottom = across ? area.removeFromBottom(std::min(scrollBarThickness, area.getHeight())) : juce::Rectangle<int>();
    view_.setBounds(area);
    const int ideal = body_->idealHeight();
    const int bodyWidth = std::max(0, area.getWidth() - (ideal > area.getHeight() ? view_.getScrollBarThickness() : 0));
    body_->setSize(bodyWidth, std::max(ideal, area.getHeight()));
    const int laneWidth = std::max(0, bodyWidth - headerWidth);
    ruler_.setBounds(headerWidth, barsHead_.getY(), laneWidth, barsHead_.getHeight());
    across_.setBounds(headerWidth, bottom.getY(), laneWidth, bottom.getHeight());
    cursorLine_->setBounds(headerWidth, 0, laneWidth, barsHead_.getHeight() + area.getHeight());
    layoutLanes();
    setViewStart(viewStart_);
    rebuildScale();
}

void Timeline::paint(juce::Graphics& g) {
    g.setColour(juce::Colours::black.withAlpha(0.3f));           // the BARS head
    g.fillRect(barsHead_);
    g.setColour(theme::hairline);
    g.fillRect(barsHead_.getRight() - 1, barsHead_.getY(), 1, barsHead_.getHeight());
    g.fillRect(barsHead_.getX(), barsHead_.getBottom() - 1, barsHead_.getWidth(), 1);
    g.setColour(theme::dim);
    g.setFont(theme::font(theme::sizeTag, true));
    g.drawText("BARS", barsHead_.reduced(8, 0), juce::Justification::centredLeft, true);
    if (across_.isVisible()) {                                    // under the header column, beside the scroll bar
        g.setColour(juce::Colours::black.withAlpha(0.18f));
        g.fillRect(0, across_.getY(), std::min(headerWidth, getWidth()), across_.getHeight());
    }
}

void Timeline::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (e.mods.isCtrlDown() || e.mods.isCommandDown()) {
        setZoom(zoom_ * std::pow(2.0, static_cast<double>(wheel.deltaY) * 2.0));
        return;
    }
    if (e.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY)) {
        const float delta = wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY;
        setViewStart(viewStart_ - static_cast<double>(delta) * visibleSeconds() * 0.5);
        rebuildScale();
        return;
    }
    view_.useMouseWheelMoveIfNeeded(e.getEventRelativeTo(&view_), wheel);
}

}  // namespace sqlive::ui
