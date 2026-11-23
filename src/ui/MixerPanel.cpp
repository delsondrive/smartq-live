#include "ui/MixerPanel.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "model/Words.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using app::LevelTarget;

namespace {

// The mockup's CSS: .console (padding 10, gap 6), .gap (10 px wide, its hairline 4 px in, 8 px short of either end), and
// the main view's .console with 28 px along the top for its caption.
constexpr int kPad = 10, kCompactTop = 28, kStripGap = 6, kDividerWidth = 10, kDividerLineX = 4, kDividerInset = 8;
constexpr int kFramesPerSecond = 30;
constexpr double kRunGapMs = 1000.0;                              // a run of arrows ends after a second without one
constexpr float kArrowDb = 0.5f, kFineArrowDb = 0.1f;
constexpr int kArrowPan = 5, kFineArrowPan = 1;

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

// Every press of the mouse anywhere while a value is typed (a global listener, on only while typing).
class MixerPanel::PressWatcher : public juce::MouseListener {
public:
    explicit PressWatcher(MixerPanel& owner) : owner_(owner) {}
    void mouseDown(const juce::MouseEvent& e) override { owner_.pressedWhileTyping(e); }

private:
    MixerPanel& owner_;
};

MixerPanel::MixerPanel(app::Session& session)
    : session_(session), meters_(session.meters()), row_(std::make_unique<Row>()), watcher_(std::make_unique<PressWatcher>(*this)) {
    viewport_.setViewedComponent(row_.get(), false);
    viewport_.setScrollBarsShown(false, true, false, true);
    addAndMakeVisible(viewport_);
    keepOffKeyboard(*this);                                       // until a strip is selected, in Edit mode
    editor_.onDone = [this](bool keep) { finishTyping(keep); };
    songSerial_ = session_.now().playingSerial;
    session_.changes().addChangeListener(this);
    startTimerHz(kFramesPerSecond);
}

MixerPanel::~MixerPanel() {
    onKeysChanged = nullptr;                                      // nothing told while it goes
    watchPresses(false);
    editor_.onDone = nullptr;                                     // nothing typed is set while it goes
    editor_.stop();
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
        strips_[i]->set(shown_[i], compact_ ? ChannelStrip::Size::Compact : ChannelStrip::Size::Full);
    resized();
    repaint();
}

const juce::Viewport& MixerPanel::viewport() const { return viewport_; }

int MixerPanel::minimumHeight() const {
    const auto size = compact_ ? ChannelStrip::Size::Compact : ChannelStrip::Size::Full;
    return ChannelStrip::minimumHeight(size) + 2 * kPad + (compact_ ? kCompactTop - kPad : 0) + viewport_.getScrollBarThickness();
}

ChannelStrip* MixerPanel::stripAt(int index) {
    return juce::isPositiveAndBelow(index, static_cast<int>(strips_.size())) ? strips_[static_cast<std::size_t>(index)].get() : nullptr;
}

const MixerState::Strip* MixerPanel::stateOf(int index) const {
    return juce::isPositiveAndBelow(index, static_cast<int>(shown_.size())) ? &shown_[static_cast<std::size_t>(index)] : nullptr;
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
    strip.onSelect = [this, index] { pressed(index); };
    strip.onTypeRequest = [this, index](ChannelStrip::Field field) {
        if (editMode() && selected_ == index) startTyping(index, field);
    };
    strip.onEffectOpen = [this, index](int effect) { openEffects(index, effect); };
    strip.onEffectSwitch = [this, index](int effect, bool on) { switchEffect(index, effect, on); };
    strip.onEffectMove = [this, index](int from, int to) { moveEffect(index, from, to); };
}

// An effect's own index, or -1 (the add menu) for the first empty slot.
void MixerPanel::openEffects(int stripIndex, int effectIndex) {
    const auto* strip = stateOf(stripIndex);
    if (strip == nullptr || !strip->effects || !editMode() || !onOpenEffects) return;
    const auto target = strip->effectsTarget;
    onOpenEffects(target, juce::isPositiveAndBelow(effectIndex, static_cast<int>(strip->effects->size())) ? effectIndex : -1);
}

void MixerPanel::switchEffect(int stripIndex, int effectIndex, bool on) {
    const auto* strip = stateOf(stripIndex);
    if (strip == nullptr || !strip->effects) return;
    const auto target = strip->effectsTarget;                     // a copy: the session's change may make the state again
    session_.switchEffect(target, effectIndex, on);
    refresh(true);
}

void MixerPanel::moveEffect(int stripIndex, int from, int to) {
    const auto* strip = stateOf(stripIndex);
    if (strip == nullptr || !strip->effects) return;
    const auto target = strip->effectsTarget;
    session_.moveEffect(target, from, to);
    refresh(true);
}

bool MixerPanel::pinned(int stripIndex) const {
    return state_.outputStrip.has_value() && stripIndex == static_cast<int>(state_.strips.size());
}

// A press starts a gesture and a release ends it; neither is a level of its own (a fader that isn't sent reports -60
// there, which isn't "off"). Each move, and a double-click or wheel notch (a gesture of its own), is, except one that
// leaves a fader that isn't sent at the bottom: a pixel's wobble, or a notch down, doesn't start sending it at -60.
void MixerPanel::faderMoved(int stripIndex, float db, bool gestureStart, bool gestureEnd) {
    endRun();                                                     // the mouse ends a run of arrows
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
    endRun();
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

void MixerPanel::changeListenerCallback(juce::ChangeBroadcaster*) {
    if (!editMode()) dropKeys();                                  // hidden or not: Show mode never has the keys
    refresh();
}

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
    case Kind::Bus: return meters_.bus(meter.bus);
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
        for (std::size_t i = 0; i < strips_.size() && i < shown_.size(); ++i) {
            read(shown_[i].meter);
            strips_[i]->clearMeter();
        }
    }
    resetPeaksIfASongStarted();
    for (std::size_t i = 0; i < strips_.size() && i < shown_.size(); ++i) strips_[i]->setMeter(read(shown_[i].meter), nowMs);
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
    if (!stateChanged && strips_.size() == shown_.size()) return;

    auto nextShown = next.strips;
    if (next.outputStrip) nextShown.push_back(*next.outputStrip);
    const bool sameStrips = nextShown.size() == strips_.size() && nextShown.size() == shown_.size()
                            && std::equal(nextShown.begin(), nextShown.end(), shown_.begin(),
                                          [](const MixerState::Strip& a, const MixerState::Strip& b) {
                                              return a.target == b.target && a.group == b.group;
                                          });
    // Meters it didn't read until now (another view, a song starting): what the reader holds for them is old.
    const bool newMeters = !sameStrips || !std::equal(nextShown.begin(), nextShown.end(), shown_.begin(),
                                                      [](const MixerState::Strip& a, const MixerState::Strip& b) { return a.meter == b.meter; });
    if (newMeters) stale_ = true;
    // What is being typed on a strip that goes (another output or song chosen, a part added) is closed with the strips as
    // they were, and set once the new ones are made.
    const auto typed = sameStrips ? std::nullopt : closeTyping();
    state_ = std::move(next);
    shown_ = std::move(nextShown);
    if (!sameStrips) {
        // Made again only when the strips themselves change, never during a drag of one of them. The output strip is held
        // on the panel itself, beside the row.
        strips_.clear();
        for (std::size_t i = 0; i < shown_.size(); ++i) {
            strips_.push_back(std::make_unique<ChannelStrip>());
            wire(*strips_.back(), static_cast<int>(i));
            if (pinned(static_cast<int>(i))) addAndMakeVisible(*strips_.back());
            else row_->addAndMakeVisible(*strips_.back());
        }
        keepOffKeyboard(*row_);
        for (auto& strip : strips_) keepOffKeyboard(*strip);
    }
    const auto size = compact_ ? ChannelStrip::Size::Compact : ChannelStrip::Size::Full;
    boxRow_ = std::any_of(shown_.begin(), shown_.end(), [](const MixerState::Strip& s) { return s.effects.has_value(); });
    for (std::size_t i = 0; i < strips_.size(); ++i) {
        strips_[i]->set(shown_[i], size);
        strips_[i]->setEffectsRow(boxRow_);
    }
    if (!sameStrips) layoutRow();
    if (!sameStrips && selected_ >= 0) {
        // The selected strip, if it is still there; otherwise none.
        const auto found = std::find_if(shown_.begin(), shown_.end(), [&](const MixerState::Strip& s) { return s.target == selectedTarget_; });
        const int index = found != shown_.end() ? static_cast<int>(found - shown_.begin()) : -1;
        setSelection(index);
        if (index < 0) {
            giveKeysBack();
            if (onStripSelected) onStripSelected(-1);
        }
    }
    if (stateChanged && onStateChanged) onStateChanged();
    if (typed) setTyped(*typed);
}

void MixerPanel::paint(juce::Graphics& g) {
    theme::paintPanel(g, getLocalBounds().toFloat());
    if (pinnedDivider_ < 0 || strips_.empty()) return;
    // The output strip's divider, as the row's, and its glow when selected (the row draws its own strips').
    const auto& out = *strips_.back();
    g.setColour(theme::hairline);
    g.fillRect(pinnedDivider_, out.getY() + kDividerInset, 1, std::max(0, out.getHeight() - 2 * kDividerInset));
    if (out.isSelected()) {
        juce::Path shape;
        shape.addRoundedRectangle(out.getBounds().toFloat(), 7.0f);
        juce::DropShadow(theme::mint.withAlpha(0.18f), 18, {}).drawForPath(g, shape);
    }
}

void MixerPanel::resized() {
    area_ = getLocalBounds().reduced(kPad);
    if (compact_) area_.setTop(std::min(area_.getBottom(), getLocalBounds().getY() + kCompactTop));
    // The viewport and its scroll bars (made again when the look changes) never take the keyboard; the strips are kept
    // off it as they are made, and the typing field, while it is open, takes it.
    viewport_.setWantsKeyboardFocus(false);
    viewport_.setMouseClickGrabsKeyboardFocus(false);
    layoutRow();
    keepOffKeyboard(viewport_.getHorizontalScrollBar());
    keepOffKeyboard(viewport_.getVerticalScrollBar());
}

// The row in the room left of the output strip (when there is one: held at the right after a divider), each strip as tall
// as the row leaves above its scroll bar.
void MixerPanel::layoutRow() {
    const int count = static_cast<int>(state_.strips.size());
    const auto size = compact_ ? ChannelStrip::Size::Compact : ChannelStrip::Size::Full;
    const int width = ChannelStrip::stripWidth(size);
    int dividers = 0;
    for (int i = 1; i < count; ++i)
        if (state_.strips[static_cast<std::size_t>(i)].group != state_.strips[static_cast<std::size_t>(i) - 1].group) ++dividers;
    const int needed = count * width + std::max(0, count - 1) * kStripGap + dividers * (kDividerWidth + kStripGap);

    // The output strip right after the roles when they all fit, as the mockup has it; held at the right when they scroll.
    auto rowArea = area_;
    juce::Rectangle<int> pinArea;
    pinnedDivider_ = -1;
    const bool hasPinned = state_.outputStrip.has_value() && static_cast<int>(strips_.size()) == count + 1;
    if (hasPinned) {
        const int pinWidth = std::min(rowArea.getWidth(), compact_ ? width : ChannelStrip::outputStripWidth);
        const int divider = 2 * kStripGap + kDividerWidth;
        if (needed + divider + pinWidth < rowArea.getWidth()) rowArea.setWidth(needed + divider + pinWidth);
        pinArea = rowArea.removeFromRight(pinWidth);
        rowArea.removeFromRight(std::min(rowArea.getWidth(), divider));
        pinnedDivider_ = rowArea.getRight() + kStripGap + kDividerLineX;
    }
    viewport_.setBounds(rowArea);

    const int available = viewport_.getWidth();
    const bool scrolls = needed > available;
    const int rowHeight = std::max(0, viewport_.getHeight() - (scrolls ? viewport_.getScrollBarThickness() : 0));
    row_->setSize(std::max(needed, available), rowHeight);

    row_->dividers.clear();
    int x = 0;
    for (int i = 0; i < count && i < static_cast<int>(strips_.size()); ++i) {
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
    if (hasPinned) strips_.back()->setBounds(pinArea.getX(), rowArea.getY(), pinArea.getWidth(), rowHeight);
    if (auto* strip = stripAt(typingStrip_)) editor_.place(typingBox(*strip, editor_.field()));
    row_->repaint();
    repaint();
}

// ---- The keyboard ----------------------------------------------------------------------------------------------------

bool MixerPanel::editMode() const { return session_.mode() == app::Mode::Edit; }

bool MixerPanel::takesKeys() const { return editMode() && selected_ >= 0; }

bool MixerPanel::hasKeys() const { return takesKeys() && hasKeyboardFocus(true); }

void MixerPanel::keysMaybeChanged() {
    const bool now = hasKeys();
    if (now == hadKeys_) return;
    hadKeys_ = now;
    if (onKeysChanged) onKeysChanged();
}

StripValueEditor* MixerPanel::typing() { return typingStrip_ >= 0 ? &editor_ : nullptr; }

void MixerPanel::selectStrip(int index) {
    endRun();
    setSelection(editMode() ? index : -1);
    if (selected_ < 0) giveKeysBack();
}

// Marks the strip (and none other) and keeps the panel's wish for the keyboard to match. Always applied, so strips made
// again are marked too.
void MixerPanel::setSelection(int index) {
    if (!juce::isPositiveAndBelow(index, static_cast<int>(strips_.size()))) index = -1;
    if (index != selected_) endRun();
    if (typingStrip_ >= 0 && typingStrip_ != index) finishTyping(true);   // as a click elsewhere would
    selected_ = index;
    if (index >= 0) selectedTarget_ = shown_[static_cast<std::size_t>(index)].target;
    for (std::size_t i = 0; i < strips_.size(); ++i) strips_[i]->setSelected(static_cast<int>(i) == selected_);
    setWantsKeyboardFocus(takesKeys());
    if (index >= 0) scrollTo(index);
    keysMaybeChanged();
}

void MixerPanel::pressed(int stripIndex) {
    if (!editMode()) return;                                      // Show mode: the mouse works, the keys stay the show's
    if (typingStrip_ >= 0) finishTyping(true);                    // a press outside the field sets what was typed
    endRun();
    const bool changed = stripIndex != selected_;
    setSelection(stripIndex);
    takeKeys();
    if (changed && onStripSelected) onStripSelected(selected_);
}

void MixerPanel::choose(int index) {
    const bool changed = index != selected_;
    setSelection(index);
    if (changed && onStripSelected) onStripSelected(selected_);
}

void MixerPanel::takeKeys() {
    if (!takesKeys()) return;
    if (!hasKeyboardFocus(true)) {
        auto* before = juce::Component::getCurrentlyFocusedComponent();
        keysBefore_ = before != nullptr && before != this && !isParentOf(before) ? before : nullptr;
    }
    if (isShowing()) grabKeyboardFocus();
}

void MixerPanel::setKeysHome(juce::Component* home) { keysHome_ = home; }

void MixerPanel::giveKeysBack() {
    setWantsKeyboardFocus(takesKeys());
    if (!hasKeyboardFocus(true)) return;
    auto* before = keysHome_ != nullptr ? keysHome_.getComponent() : keysBefore_.getComponent();
    keysBefore_ = nullptr;
    if (before != nullptr && before->isShowing()) before->grabKeyboardFocus();
    else giveAwayKeyboardFocus();
}

void MixerPanel::dropKeys() {
    if (typingStrip_ >= 0) finishTyping(false);
    endRun();
    if (selected_ < 0) {
        setWantsKeyboardFocus(false);
        return;
    }
    setSelection(-1);
    giveKeysBack();
    if (onStripSelected) onStripSelected(-1);
}

void MixerPanel::focusGained(FocusChangeType) { keysMaybeChanged(); }

void MixerPanel::focusLost(FocusChangeType) {
    endRun();
    keysMaybeChanged();
}

void MixerPanel::focusOfChildComponentChanged(FocusChangeType) { keysMaybeChanged(); }

bool MixerPanel::keyPressed(const juce::KeyPress& key) {
    if (!takesKeys()) return false;
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    const bool shift = mods.isShiftDown();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown()) {   // the app's (Ctrl+Z, Ctrl+Y), never the mixer's
        endRun();
        return false;
    }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) {
        stepLevel(selected_, (code == juce::KeyPress::upKey ? 1.0f : -1.0f) * (shift ? kFineArrowDb : kArrowDb));
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {
        stepPan(selected_, (code == juce::KeyPress::rightKey ? 1 : -1) * (shift ? kFineArrowPan : kArrowPan));
        return true;
    }
    endRun();                                                     // any other key ends a run of arrows
    const int count = static_cast<int>(strips_.size());
    if (code == juce::KeyPress::tabKey) {
        if (count > 0) choose((selected_ + (shift ? count - 1 : 1)) % count);
        return true;
    }
    if (code == juce::KeyPress::escapeKey) {
        setSelection(-1);
        giveKeysBack();
        if (onStripSelected) onStripSelected(-1);
        return true;
    }
    if (code == juce::KeyPress::returnKey) {
        startTyping(selected_, shift ? ChannelStrip::Field::Pan : ChannelStrip::Field::Level);
        return true;
    }
    const auto letter = juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(code));
    if (letter == 'm') {
        toggleMute(selected_);
        return true;
    }
    if (letter == 's') {
        if (const auto* strip = stateOf(selected_); strip != nullptr && strip->canSolo) setSoloed(selected_, !strip->soloed);
        return true;
    }
    return false;                                                 // Space and the rest: the page's, the app's
}

juce::String MixerPanel::runGesture(int stripIndex) {
    const double now = clockMs ? clockMs() : juce::Time::getMillisecondCounterHiRes();
    if (runStrip_ != stripIndex || now - runLastMs_ > kRunGapMs || runGesture_.isEmpty()) {
        runGesture_ = juce::Uuid().toString();
        runMovedLevel_ = runMovedPan_ = false;
    }
    runStrip_ = stripIndex;
    runLastMs_ = now;
    return runGesture_;
}

// A run that moved both the level and the pan is one undo step, named for both ("level and pan Keys", "level and pan Keys
// on Headphones"); the session names one that moved only one of them.
void MixerPanel::nameRun(const app::LevelTarget& target, const juce::String& name) {
    if (!runMovedLevel_ || !runMovedPan_) return;
    auto who = name;
    if (target.kind == app::LevelTarget::Kind::Mix)
        if (const auto* rig = session_.model().rig(session_.rigId()))
            for (const auto& output : rig->outputs)
                if (output.id == target.outputId) who += " on " + output.name;
    session_.commands().nameLastStep(runGesture_, "level and pan " + who);
}

// From the session, not the state shown: an undo may have changed it since the strips were last drawn.
void MixerPanel::stepLevel(int stripIndex, float stepDb) {
    const auto* strip = stateOf(stripIndex);
    if (strip == nullptr) return;
    const auto target = strip->target;
    const auto name = strip->name;
    const auto gesture = runGesture(stripIndex);
    const auto now = session_.level(target);
    if (!now && stepDb < 0.0f) return;                            // not sent: down leaves it so
    const float next = now ? toTenth(juce::jlimit(ChannelStrip::kMinDb, ChannelStrip::kMaxDb, *now + stepDb)) : ChannelStrip::kMinDb;
    if (now && *now == next) return;
    session_.setLevel(target, next, gesture);
    runMovedLevel_ = true;
    nameRun(target, name);
    refresh(true);
}

// A linked role (no pan of its own) starts at the centre, as the strip shows it; its first step unlinks it.
void MixerPanel::stepPan(int stripIndex, int step) {
    const auto* strip = stateOf(stripIndex);
    if (strip == nullptr || !strip->pan) return;
    const auto target = strip->target;
    const auto name = strip->name;
    const auto gesture = runGesture(stripIndex);
    const auto own = session_.pan(target);
    const int now = own.value_or(0);
    const int next = juce::jlimit(-100, 100, now + step);
    if (own && next == now) return;
    session_.setPan(target, next, gesture);
    runMovedPan_ = true;
    nameRun(target, name);
    refresh(true);
}

// Only where the strip has the box: a compact strip (the main view's, in Show mode) has no level box, so nothing to type in.
juce::Rectangle<int> MixerPanel::typingBox(ChannelStrip& strip, ChannelStrip::Field field) const {
    return field == ChannelStrip::Field::Pan ? strip.panBoxArea() : strip.levelBoxArea();
}

void MixerPanel::startTyping(int stripIndex, ChannelStrip::Field field) {
    auto* strip = stripAt(stripIndex);
    const auto* state = stateOf(stripIndex);
    if (!editMode() || strip == nullptr || state == nullptr) return;
    if (typingBox(*strip, field).isEmpty()) return;               // no pan bar, or no level box, here
    if (typingStrip_ >= 0) finishTyping(true);
    endRun();
    typingStrip_ = stripIndex;
    typingFrom_ = field == ChannelStrip::Field::Level ? strip->levelText() : model::formatPan(state->linked ? 0 : *state->pan);
    // The press that opened it (on the level box or the pan's words), if one did, is no press outside it.
    openingPress_ = juce::Desktop::getInstance().getMainMouseSource().getLastMouseDownTime();
    editor_.start(*strip, field, typingBox(*strip, field), typingFrom_);
    watchPresses(true);
}

// While typing, every press of the mouse anywhere is seen (after the component pressed has had it): one outside the
// field sets what was typed, as Enter would. Never on while nothing is typed, so never in Show mode.
void MixerPanel::watchPresses(bool on) {
    if (on == watching_) return;
    watching_ = on;
    if (on) juce::Desktop::getInstance().addGlobalMouseListener(watcher_.get());
    else juce::Desktop::getInstance().removeGlobalMouseListener(watcher_.get());
}

void MixerPanel::pressedWhileTyping(const juce::MouseEvent& e) {
    auto* pressed = e.eventComponent;
    if (typingStrip_ < 0 || pressed == nullptr || pressed == &editor_ || editor_.isParentOf(pressed)) return;
    if (e.mouseDownTime == openingPress_) return;
    finishTyping(true);
}

// Closes the field. The keys come back to the mixer only from the field itself (Enter, Esc), never from where a click took
// them. What it held, if the words were changed from what the box said; nothing otherwise.
std::optional<MixerPanel::Typed> MixerPanel::closeTyping() {
    if (typingStrip_ < 0) return std::nullopt;
    const int index = std::exchange(typingStrip_, -1);
    watchPresses(false);
    const auto field = editor_.field();
    const auto text = editor_.text().trim();
    const bool hadKeys = editor_.hasKeyboardFocus(true);
    editor_.stop();
    if (hadKeys && takesKeys() && isShowing()) grabKeyboardFocus();
    const auto* strip = stateOf(index);
    if (strip == nullptr || text == typingFrom_.trim()) return std::nullopt;
    return Typed{strip->target, field, text};
}

// Enter, Esc, the keyboard gone elsewhere, a press outside the field.
void MixerPanel::finishTyping(bool keep) {
    const auto typed = closeTyping();
    if (keep && typed) setTyped(*typed);
}

// What can't be read sets nothing, and its box flashes (when its strip is still shown).
void MixerPanel::setTyped(const Typed& typed) {
    if (!editMode()) return;
    const auto flash = [&] {
        const auto found = std::find_if(shown_.begin(), shown_.end(), [&](const MixerState::Strip& s) { return s.target == typed.target; });
        if (auto* view = stripAt(found != shown_.end() ? static_cast<int>(found - shown_.begin()) : -1)) view->flash(typed.field);
    };
    if (typed.field == ChannelStrip::Field::Level) {
        const auto db = model::parseDb(typed.text);
        const bool off = db && std::isinf(*db);
        if (!db || (off && typed.target.kind != app::LevelTarget::Kind::Mix)) {   // "off" is an output's "not sent" only
            flash();
            return;
        }
        session_.setLevel(typed.target, off ? std::nullopt : std::optional<float>(*db), juce::Uuid().toString());
    } else {
        const auto pan = model::parsePan(typed.text);
        if (!pan) {
            flash();
            return;
        }
        session_.setPan(typed.target, *pan, juce::Uuid().toString());
    }
    refresh(true);
}

void MixerPanel::scrollTo(int stripIndex) {
    const auto* strip = stripAt(stripIndex);
    if (strip == nullptr || pinned(stripIndex)) return;           // the output strip is always in view
    const auto bounds = strip->getBounds();
    const int width = viewport_.getViewWidth();
    int x = viewport_.getViewPositionX();
    if (bounds.getX() < x) x = bounds.getX();
    else if (bounds.getRight() > x + width) x = bounds.getRight() - width;
    viewport_.setViewPosition(x, viewport_.getViewPositionY());
}

}  // namespace sqlive::ui
