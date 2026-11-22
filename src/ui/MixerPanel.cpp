#include "ui/MixerPanel.h"

#include <algorithm>

#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using app::LevelTarget;

namespace {

// The mockup's CSS: .console (padding 10, gap 6), .gap (10 px wide, its hairline 4 px in, 8 px short of either end), and
// the main view's .console with 28 px along the top for its caption.
constexpr int kPad = 10, kCompactTop = 28, kStripGap = 6, kDividerWidth = 10, kDividerLineX = 4, kDividerInset = 8;
constexpr int kFramesPerSecond = 30;

}  // namespace

// The strips side by side, with a thin line between groups, and the selected strip's glow; what the viewport scrolls.
class MixerPanel::Row : public juce::Component {
public:
    Row() { setInterceptsMouseClicks(false, true); }
    std::vector<int> dividers;                                    // x of each divider's line

    void paint(juce::Graphics& g) override {
        g.setColour(theme::hairline);
        for (const int x : dividers) g.fillRect(x, kDividerInset, 1, std::max(0, getHeight() - 2 * kDividerInset));
        // The mockup's .strip.sel: a soft mint glow around the selected strip.
        for (auto* child : getChildren())
            if (const auto* strip = dynamic_cast<const ChannelStrip*>(child); strip != nullptr && strip->isSelected()) {
                juce::Path shape;
                shape.addRoundedRectangle(strip->getBounds().toFloat(), 7.0f);
                juce::DropShadow(theme::mint.withAlpha(0.18f), 18, {}).drawForPath(g, shape);
            }
    }
};

MixerPanel::MixerPanel(app::Session& session)
    : session_(session), meters_(session.meters()), row_(std::make_unique<Row>()) {
    viewport_.setViewedComponent(row_.get(), false);
    viewport_.setScrollBarsShown(false, true, false, true);
    addAndMakeVisible(viewport_);
    keepOffKeyboard(*this);
    songSerial_ = session_.now().playingSerial;
    session_.changes().addChangeListener(this);
    startTimerHz(kFramesPerSecond);
}

MixerPanel::~MixerPanel() {
    stopTimer();
    session_.changes().removeChangeListener(this);
}

void MixerPanel::show(const juce::String& songId, const juce::String& outputId) {
    songId_ = songId;
    outputId_ = outputId;
    refresh(true);
}

void MixerPanel::setCompact(bool compact) {
    if (compact == compact_) return;
    compact_ = compact;
    for (std::size_t i = 0; i < strips_.size(); ++i)
        strips_[i]->set(state_.strips[i], compact_ ? ChannelStrip::Size::Compact : ChannelStrip::Size::Full);
    resized();
    repaint();
}

const juce::Viewport& MixerPanel::viewport() const { return viewport_; }

ChannelStrip* MixerPanel::stripAt(int index) {
    return juce::isPositiveAndBelow(index, static_cast<int>(strips_.size())) ? strips_[static_cast<std::size_t>(index)].get() : nullptr;
}

const MixerState::Strip* MixerPanel::stateOf(int index) const {
    return juce::isPositiveAndBelow(index, static_cast<int>(state_.strips.size())) ? &state_.strips[static_cast<std::size_t>(index)] : nullptr;
}

void MixerPanel::toggleMute(int stripIndex) {
    const auto* found = stateOf(stripIndex);
    if (found == nullptr || !found->canMute) return;
    const auto strip = *found;
    if (strip.target.kind == LevelTarget::Kind::Part)
        session_.setMute(strip.target.songId, strip.target.partId, !strip.muted);
    else if (strip.target.kind == LevelTarget::Kind::Mix)
        session_.setLevel(strip.target, strip.muted ? std::optional<float>(0.0f) : std::nullopt, juce::Uuid().toString());
    refresh(true);
}

void MixerPanel::wire(ChannelStrip& strip, int index) {
    strip.onLevelDrag = [this, index](float db, bool start, bool end) { faderMoved(index, db, start, end); };
    strip.onPanDrag = [this, index](int pan, bool start, bool end) { panMoved(index, pan, start, end); };
    strip.onMute = [this, index](bool) { toggleMute(index); };
    strip.onSolo = [this, index](bool on) { setSoloed(index, on); };
    strip.onLink = [this, index](bool linked) { setLinked(index, linked); };
}

// A press starts a gesture and a release ends it; neither is a level of its own (a fader that isn't sent reports -60
// there, which isn't "off"). Each move, and a double-click or wheel notch (a gesture of its own), is, except one that
// leaves a fader that isn't sent at the bottom: a pixel's wobble, or a notch down, doesn't start sending it at -60.
void MixerPanel::faderMoved(int stripIndex, float db, bool gestureStart, bool gestureEnd) {
    const auto* strip = stateOf(stripIndex);
    if (strip == nullptr) return;
    if (gestureStart) gesture_ = juce::Uuid().toString();
    if (gestureStart != gestureEnd) return;
    if (!strip->db && db <= ChannelStrip::kMinDb) {
        strips_[static_cast<std::size_t>(stripIndex)]->set(*strip, compact_ ? ChannelStrip::Size::Compact : ChannelStrip::Size::Full);   // back to "not sent"
        return;
    }
    session_.setLevel(strip->target, db, gesture_);
    refresh(true);
}

// As the fader: the press and the release are no pan of their own. A move on a linked role's bar gives it its own pan,
// unlinking it, in the same gesture (the session makes it one undo step, or tonight's own pan in a show).
void MixerPanel::panMoved(int stripIndex, int pan, bool gestureStart, bool gestureEnd) {
    const auto* strip = stateOf(stripIndex);
    if (strip == nullptr || !strip->pan) return;
    if (gestureStart) gesture_ = juce::Uuid().toString();
    if (gestureStart != gestureEnd) return;
    session_.setPan(strip->target, pan, gesture_);
    refresh(true);
}

// Unlinking starts at the centre: an output view covers every song, so there is no one song pan to start from.
void MixerPanel::setLinked(int stripIndex, bool linked) {
    const auto* strip = stateOf(stripIndex);
    if (strip == nullptr || !strip->linkable) return;
    session_.setPan(strip->target, linked ? std::nullopt : std::optional<int>(0), juce::Uuid().toString());
    refresh(true);
}

void MixerPanel::setSoloed(int stripIndex, bool soloed) {
    const auto* strip = stateOf(stripIndex);
    if (strip == nullptr || !strip->canSolo) return;
    session_.setSolo(strip->target.songId, strip->target.partId, soloed);
    refresh(true);
}

void MixerPanel::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(); }

void MixerPanel::visibilityChanged() {
    if (!isVisible()) stale_ = true;
    else if (!fresh_) refresh();
}

void MixerPanel::timerCallback() {
    if (shownWithParents(*this)) tickMeters(juce::Time::getMillisecondCounterHiRes());
    else stale_ = true;
}

void MixerPanel::resetPeaksIfASongStarted() {
    const auto serial = session_.now().playingSerial;
    if (serial == songSerial_) return;
    songSerial_ = serial;
    if (serial == 0) return;                                      // stopped: the peaks stay to be read
    for (auto& strip : strips_) strip->resetPeak();
}

app::MeterReader::Peak MixerPanel::read(const MixerState::Meter& meter) {
    using Kind = MixerState::Meter::Kind;
    switch (meter.kind) {
    case Kind::Part: return meters_.part(meter.index);
    case Kind::Click: return meters_.click();
    case Kind::Cues: return meters_.cues();
    case Kind::RoleOnBus: return meters_.roleOnBus(meter.index, meter.bus);
    case Kind::None: break;
    }
    return {};
}

// The reader gives everything since it last asked for a meter. After a while without frames (hidden), or with meters it
// didn't read before, that is old music, perhaps another song's: it is read and dropped, and every bar, hold line and clip
// light starts from nothing, before the first frame is drawn. (The peak boxes keep the loudest moment unless a song
// started, as they would have.) Hidden, there are no frames.
void MixerPanel::tickMeters(double nowMs) {
    if (!shownWithParents(*this)) {
        stale_ = true;
        return;
    }
    if (stale_) {
        stale_ = false;
        if (!fresh_) refresh();                                   // the meters as the view shows them now
        for (std::size_t i = 0; i < strips_.size() && i < state_.strips.size(); ++i) {
            read(state_.strips[i].meter);
            strips_[i]->clearMeter();
        }
    }
    resetPeaksIfASongStarted();
    for (std::size_t i = 0; i < strips_.size() && i < state_.strips.size(); ++i) strips_[i]->setMeter(read(state_.strips[i].meter), nowMs);
}

void MixerPanel::refresh(bool force) {
    // Hidden, it waits until it is shown: the session's changes come 30 times a second while a song plays.
    if (!force && !shownWithParents(*this)) {
        fresh_ = false;
        return;
    }
    fresh_ = true;
    ++states_made_;
    auto next = makeMixerState(session_, songId_, outputId_);
    const bool stateChanged = !(next == state_);
    if (!stateChanged && strips_.size() == state_.strips.size()) return;

    const bool sameStrips = next.strips.size() == strips_.size()
                            && std::equal(next.strips.begin(), next.strips.end(), state_.strips.begin(),
                                          [](const MixerState::Strip& a, const MixerState::Strip& b) {
                                              return a.target == b.target && a.group == b.group;
                                          });
    // Meters it didn't read until now (another view, a song starting): what the reader holds for them is old.
    const bool newMeters = !sameStrips || !std::equal(next.strips.begin(), next.strips.end(), state_.strips.begin(),
                                                      [](const MixerState::Strip& a, const MixerState::Strip& b) { return a.meter == b.meter; });
    if (newMeters) stale_ = true;
    state_ = std::move(next);
    if (!sameStrips) {
        // Made again only when the strips themselves change, never during a drag of one of them.
        strips_.clear();
        for (std::size_t i = 0; i < state_.strips.size(); ++i) {
            strips_.push_back(std::make_unique<ChannelStrip>());
            wire(*strips_.back(), static_cast<int>(i));
            row_->addAndMakeVisible(*strips_.back());
        }
        keepOffKeyboard(*row_);
    }
    const auto size = compact_ ? ChannelStrip::Size::Compact : ChannelStrip::Size::Full;
    for (std::size_t i = 0; i < strips_.size(); ++i) strips_[i]->set(state_.strips[i], size);
    if (!sameStrips) layoutRow();
    if (stateChanged && onStateChanged) onStateChanged();
}

void MixerPanel::paint(juce::Graphics& g) { theme::paintPanel(g, getLocalBounds().toFloat()); }

void MixerPanel::resized() {
    auto area = getLocalBounds().reduced(kPad);
    if (compact_) area.setTop(std::min(area.getBottom(), getLocalBounds().getY() + kCompactTop));
    viewport_.setBounds(area);
    keepOffKeyboard(viewport_);                                   // the scroll bars, made again when the look changes
    layoutRow();
}

void MixerPanel::layoutRow() {
    const int count = static_cast<int>(strips_.size());
    const int width = ChannelStrip::stripWidth(compact_ ? ChannelStrip::Size::Compact : ChannelStrip::Size::Full);
    int dividers = 0;
    for (int i = 1; i < count; ++i)
        if (state_.strips[static_cast<std::size_t>(i)].group != state_.strips[static_cast<std::size_t>(i) - 1].group) ++dividers;

    const int needed = count * width + std::max(0, count - 1) * kStripGap + dividers * (kDividerWidth + kStripGap);
    const int available = viewport_.getWidth();
    const bool scrolls = needed > available;
    const int rowHeight = std::max(0, viewport_.getHeight() - (scrolls ? viewport_.getScrollBarThickness() : 0));
    row_->setSize(std::max(needed, available), rowHeight);

    row_->dividers.clear();
    int x = 0;
    for (int i = 0; i < count; ++i) {
        if (i > 0) {
            x += kStripGap;
            if (state_.strips[static_cast<std::size_t>(i)].group != state_.strips[static_cast<std::size_t>(i) - 1].group) {
                row_->dividers.push_back(x + kDividerLineX);
                x += kDividerWidth + kStripGap;
            }
        }
        strips_[static_cast<std::size_t>(i)]->setBounds(x, 0, width, rowHeight);
        x += width;
    }
    row_->repaint();
}

}  // namespace sqlive::ui
