#include "ui/ChannelStrip.h"

#include <algorithm>
#include <cmath>

#include "model/Units.h"
#include "model/Words.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/StripParts.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace parts = stripparts;

namespace {

// The mockup's CSS: .strip (112 px, border 1, padding 9 7 7, gap 7; compact 118 and padding-top 8), .pan (96 x 18; 74
// beside the 18 px .link, 4 apart), .ms b (30 x 20, 5 apart), .num (46 x 20, 4 apart), .fz (at least 120 high) with
// .groove (left 27, 4 wide), .cap (left 14, 30 x 46), .scale (left 46, 26 wide), .meter (left 78, 16 wide, two bars 2
// apart), .cliplt (left 78, 16 x 5, at the top), all 10 px in from the zone's top and bottom; .scrib (about 42 high).
constexpr int kWidth = 112, kCompactWidth = 118, kBorder = 1, kPadX = 7, kPadTop = 9, kCompactPadTop = 8, kPadBottom = 7, kGap = 7;
constexpr int kPanWidth = 96, kPanLinkedWidth = 74, kPanHeight = 18, kLinkSize = 18, kLinkGap = 4;
constexpr int kButtonWidth = 30, kButtonHeight = 20, kButtonGap = 5, kBoxWidth = 46, kBoxHeight = 20, kBoxGap = 4;
constexpr int kMinZone = 120, kNameHeight = 42;
constexpr int kZoneInset = 10, kGrooveX = 27, kGrooveWidth = 4, kCapX = 14, kScaleX = 46, kScaleWidth = 26, kTickRoom = 4;
constexpr int kMeterX = 78, kMeterWidth = 16, kBarWidth = 7, kBarGap = 2, kClipHeight = 5;
constexpr float kCapWidth = 30.0f, kCapHeight = 46.0f, kDimmed = 0.55f;

constexpr float kWheelStepDb = 0.5f, kFineDrag = 0.25f, kSmoothWheelNotch = 0.1f, kPanRange = 200.0f;
constexpr float kFallDb = 1.5f;
constexpr double kHoldMs = 1500.0, kClipMs = 2000.0;
constexpr int kFlashMs = 330, kWordsRoom = 10, kMinWordsWidth = 24;

// The travel's three stretches: (fraction at the start, dB at the start), up to the next.
constexpr float kKneeLowFraction = 0.15f, kKneeLowDb = -30.0f, kKneeHighFraction = 0.75f, kKneeHighDb = 0.0f;

const juce::String kMinusInfinity = juce::String::fromUTF8("\xe2\x88\x92\xe2\x88\x9e");   // "−∞"

}  // namespace

// ---- The travel ------------------------------------------------------------------------------------------------------

float ChannelStrip::dbAtFraction(float fromBottom01) {
    const float f = juce::jlimit(0.0f, 1.0f, fromBottom01);
    if (f <= kKneeLowFraction) return juce::jmap(f, 0.0f, kKneeLowFraction, kMinDb, kKneeLowDb);
    if (f <= kKneeHighFraction) return juce::jmap(f, kKneeLowFraction, kKneeHighFraction, kKneeLowDb, kKneeHighDb);
    return juce::jmap(f, kKneeHighFraction, 1.0f, kKneeHighDb, kMaxDb);
}

float ChannelStrip::fractionForDb(float db) {
    const float d = juce::jlimit(kMinDb, kMaxDb, db);
    if (d <= kKneeLowDb) return juce::jmap(d, kMinDb, kKneeLowDb, 0.0f, kKneeLowFraction);
    if (d <= kKneeHighDb) return juce::jmap(d, kKneeLowDb, kKneeHighDb, kKneeLowFraction, kKneeHighFraction);
    return juce::jmap(d, kKneeHighDb, kMaxDb, kKneeHighFraction, 1.0f);
}

int ChannelStrip::stripWidth(Size size) { return size == Size::Compact ? kCompactWidth : kWidth; }

int ChannelStrip::minimumHeight(Size size) {
    const bool full = size == Size::Full;
    return 2 * kBorder + (full ? kPadTop : kCompactPadTop) + kPanHeight + kGap + kButtonHeight + kGap
           + (full ? kBoxHeight + kGap : 0) + kMinZone + kGap + kNameHeight + kPadBottom;
}

// ---- State -----------------------------------------------------------------------------------------------------------

ChannelStrip::ChannelStrip() { keepOffKeyboard(*this); }

void ChannelStrip::set(const MixerState::Strip& strip, Size size) {
    if (strip == strip_ && size == size_) return;
    const bool reshaped = size != size_ || strip.canSolo != strip_.canSolo || strip.linkable != strip_.linkable;
    strip_ = strip;
    size_ = size;
    if (silent()) level_[0] = level_[1] = hold_[0] = hold_[1] = kMinDb;
    if (reshaped) resized();
    repaint();
}

void ChannelStrip::setSelected(bool selected) {
    if (selected == selected_) return;
    selected_ = selected;
    repaint();
    if (auto* parent = getParentComponent()) parent->repaint(getBounds().expanded(20));   // the row draws the glow
}

void ChannelStrip::setMeter(app::MeterReader::Peak now) { setMeter(now, juce::Time::getMillisecondCounterHiRes()); }

void ChannelStrip::setMeter(app::MeterReader::Peak now, double nowMs) {
    const bool quiet = silent();
    const float sides[2] = {quiet ? 0.0f : now.left, quiet ? 0.0f : now.right};
    bool changed = false;
    float loudest = kMinDb;
    for (int s = 0; s < 2; ++s) {
        const float db = sides[s] > 0.0f ? std::max(kMinDb, model::gainToDb(sides[s])) : kMinDb;
        const float level = quiet ? kMinDb : std::max(db, std::max(kMinDb, level_[s] - kFallDb));
        float hold = hold_[s];
        if (quiet) {
            hold = kMinDb;
        } else if (db >= hold || nowMs >= holdUntil_[s]) {
            hold = db;
            holdUntil_[s] = nowMs + kHoldMs;
        }
        changed = changed || level != level_[s] || hold != hold_[s];
        level_[s] = level;
        hold_[s] = hold;
        loudest = std::max(loudest, db);
    }
    if (std::max(sides[0], sides[1]) > 1.0f) clipUntil_ = nowMs + kClipMs;
    const bool lit = clipUntil_ >= 0.0 && nowMs < clipUntil_;
    changed = changed || lit != clipLit_;
    clipLit_ = lit;
    if (loudest > kMinDb && (!peak_ || loudest > *peak_)) {
        peak_ = loudest;
        changed = true;
    }
    if (changed) {
        repaint(zone_);
        repaint(boxRow_);
    }
}

void ChannelStrip::clearMeter() {
    if (level_[0] == kMinDb && level_[1] == kMinDb && hold_[0] == kMinDb && hold_[1] == kMinDb && !clipLit_) return;
    level_[0] = level_[1] = hold_[0] = hold_[1] = kMinDb;
    holdUntil_[0] = holdUntil_[1] = 0.0;
    clipUntil_ = -1.0;
    clipLit_ = false;
    repaint(zone_);
}

void ChannelStrip::resetPeak() {
    if (!peak_) return;
    peak_.reset();
    repaint(boxRow_);
}

void ChannelStrip::flash(Field field) {
    flash_ = field;
    const int ticket = ++flashTicket_;
    repaint();
    juce::Timer::callAfterDelay(kFlashMs, [safe = juce::Component::SafePointer<ChannelStrip>(this), ticket] {
        if (safe == nullptr || safe->flashTicket_ != ticket) return;
        safe->flash_.reset();
        safe->repaint();
    });
}

juce::String ChannelStrip::levelText() const { return strip_.db ? parts::withTrueMinus(formatStripDb(*strip_.db)) : juce::String("off"); }

juce::String ChannelStrip::peakText() const { return peak_ ? parts::withTrueMinus(formatStripDb(*peak_)) : kMinusInfinity; }

juce::String ChannelStrip::panText() const {
    if (!hasPanBar()) return {};
    return strip_.linked ? juce::String("Follow") : model::formatPan(*strip_.pan);
}

// ---- Where things are ------------------------------------------------------------------------------------------------

void ChannelStrip::resized() {
    const bool full = size_ == Size::Full;
    const int x = kBorder + kPadX, width = std::max(0, getWidth() - 2 * (kBorder + kPadX));
    int y = kBorder + (full ? kPadTop : kCompactPadTop);
    const int bottom = getHeight() - kBorder - kPadBottom;
    panRow_ = {x, y, width, kPanHeight};
    y += kPanHeight + kGap;
    buttonRow_ = {x, y, width, kButtonHeight};
    y += kButtonHeight + kGap;
    if (full) {
        boxRow_ = {x, y, width, kBoxHeight};
        y += kBoxHeight + kGap;
    } else {
        boxRow_ = {};
    }
    name_ = {x, std::max(y, bottom - kNameHeight), width, std::min(kNameHeight, std::max(0, bottom - y))};
    zone_ = {x, y, width, std::max(0, name_.getY() - kGap - y)};
}

juce::Rectangle<int> ChannelStrip::panArea() const {
    if (!hasPanBar()) return {};
    const int left = panRow_.getCentreX() - kPanWidth / 2;
    return {left, panRow_.getY(), showsLink() ? kPanLinkedWidth : kPanWidth, kPanHeight};
}

// The words are drawn centred on the bar (StripParts' paintPanBar); a few pixels of room either side of them.
juce::Rectangle<int> ChannelStrip::panTextArea() const {
    const auto bar = panArea();
    if (bar.isEmpty()) return {};
    const int words = widthInPixels(theme::font(theme::sizeCaption, true), panText()) + kWordsRoom;
    return bar.withSizeKeepingCentre(juce::jlimit(std::min(kMinWordsWidth, bar.getWidth()), bar.getWidth(), words), bar.getHeight());
}

juce::Rectangle<int> ChannelStrip::linkArea() const {
    if (!showsLink()) return {};
    return {panArea().getRight() + kLinkGap, panRow_.getY(), kLinkSize, kLinkSize};
}

juce::Rectangle<int> ChannelStrip::muteArea() const {
    if (!strip_.canMute) return {};
    const int both = showsSolo() ? 2 * kButtonWidth + kButtonGap : kButtonWidth;
    return {buttonRow_.getCentreX() - both / 2, buttonRow_.getY(), kButtonWidth, kButtonHeight};
}

juce::Rectangle<int> ChannelStrip::soloArea() const {
    if (!showsSolo()) return {};
    const int left = buttonRow_.getCentreX() - (2 * kButtonWidth + kButtonGap) / 2 + kButtonWidth + kButtonGap;
    return {left, buttonRow_.getY(), kButtonWidth, kButtonHeight};
}

juce::Rectangle<int> ChannelStrip::levelBoxArea() const {
    if (boxRow_.isEmpty()) return {};
    return {boxRow_.getCentreX() - (2 * kBoxWidth + kBoxGap) / 2, boxRow_.getY(), kBoxWidth, kBoxHeight};
}

juce::Rectangle<int> ChannelStrip::peakBoxArea() const {
    if (boxRow_.isEmpty()) return {};
    return levelBoxArea().translated(kBoxWidth + kBoxGap, 0);
}

juce::Rectangle<int> ChannelStrip::scaleArea() const {
    return {zone_.getX() + kScaleX, zone_.getY(), kScaleWidth + kTickRoom, zone_.getHeight()};
}

juce::Rectangle<int> ChannelStrip::meterArea() const {
    return {zone_.getX() + kMeterX, zone_.getY() + kZoneInset, kMeterWidth, std::max(0, zone_.getHeight() - 2 * kZoneInset)};
}

juce::Rectangle<int> ChannelStrip::clipLightArea() const { return {zone_.getX() + kMeterX, zone_.getY(), kMeterWidth, kClipHeight}; }

juce::Rectangle<int> ChannelStrip::faderArea() const { return zone_.withWidth(std::min(zone_.getWidth(), kScaleX)); }

float ChannelStrip::travel() const { return std::max(1.0f, static_cast<float>(zone_.getHeight() - 2 * kZoneInset)); }

float ChannelStrip::yForDb(float db) const {
    return static_cast<float>(zone_.getBottom() - kZoneInset) - fractionForDb(db) * travel();
}

float ChannelStrip::currentDb() const { return strip_.db ? juce::jlimit(kMinDb, kMaxDb, *strip_.db) : kMinDb; }

juce::Rectangle<float> ChannelStrip::capBounds() const {
    return juce::Rectangle<float>(static_cast<float>(zone_.getX() + kCapX), yForDb(currentDb()) - 0.5f * kCapHeight, kCapWidth, kCapHeight);
}

// ---- Drawing ---------------------------------------------------------------------------------------------------------

void ChannelStrip::paint(juce::Graphics& g) {
    parts::paintBody(g, getLocalBounds().toFloat(), strip_.colour, selected_);
    const bool dimmed = strip_.silencedBySolo;

    if (hasPanBar()) {
        if (dimmed) g.beginTransparencyLayer(kDimmed);
        parts::paintPanBar(g, panArea().toFloat(), strip_.colour, strip_.linked ? std::nullopt : strip_.pan, strip_.panChangedTonight);
        if (dimmed) g.endTransparencyLayer();
        if (showsLink()) parts::paintLinkButton(g, linkArea().toFloat(), strip_.linked);
        if (flash_ == Field::Pan) parts::paintNumberBox(g, panArea().toFloat(), panText(), theme::text, true);   // couldn't be read
    }

    if (strip_.canMute) {
        const auto look = strip_.muted ? parts::ButtonLook::On : strip_.silencedBySolo ? parts::ButtonLook::Hollow : parts::ButtonLook::Off;
        parts::paintButton(g, muteArea().toFloat(), "M", look, theme::danger);
    }
    if (showsSolo())
        parts::paintButton(g, soloArea().toFloat(), "S", strip_.soloed ? parts::ButtonLook::On : parts::ButtonLook::Off, theme::caution);

    if (!boxRow_.isEmpty()) {
        parts::paintNumberBox(g, levelBoxArea().toFloat(), levelText(), strip_.changedTonight ? theme::caution : theme::text,
                              flash_ == Field::Level);
        parts::paintNumberBox(g, peakBoxArea().toFloat(), peakText(), theme::dim, peak_ && *peak_ > 0.0f);
    }

    if (zone_.getHeight() > 2 * kZoneInset) {
        const auto y = [this](float db) { return yForDb(db); };
        if (dimmed) g.beginTransparencyLayer(kDimmed);
        parts::paintClipLight(g, clipLightArea().toFloat(), clipLit_);
        parts::paintGroove(g, juce::Rectangle<int>(zone_.getX() + kGrooveX, zone_.getY() + kZoneInset, kGrooveWidth, zone_.getHeight() - 2 * kZoneInset).toFloat());
        parts::paintScale(g, juce::Rectangle<int>(zone_.getX() + kScaleX, zone_.getY(), kScaleWidth, zone_.getHeight()).toFloat(), y);
        const auto meter = meterArea().toFloat();
        const bool quiet = silent();
        for (int side = 0; side < 2; ++side)
            parts::paintMeterBar(g, meter.withX(meter.getX() + static_cast<float>(side * (kBarWidth + kBarGap))).withWidth(kBarWidth),
                                 strip_.colour, quiet ? kMinDb : level_[side], quiet ? kMinDb : hold_[side], y);
        parts::paintCap(g, capBounds(), strip_.changedTonight, !strip_.db.has_value());
        if (dimmed) g.endTransparencyLayer();
    }

    const bool footer = size_ == Size::Full || strip_.footerIsWarning;
    parts::paintNameLabel(g, name_.toFloat(), strip_.name, strip_.colour, strip_.inEarsOnly, footer ? strip_.footer : juce::String(),
                          strip_.footerIsWarning, selected_);
}

// ---- The mouse -------------------------------------------------------------------------------------------------------

void ChannelStrip::mouseDown(const juce::MouseEvent& e) {
    ++typeTicket_;                                                // a second press: no typed pan after all
    if (e.mods.isPopupMenu()) return;
    if (onSelect) onSelect();
    const auto at = e.getPosition();
    if (linkArea().contains(at)) {
        if (onLink) onLink(!strip_.linked);
    } else if (muteArea().contains(at)) {
        if (onMute) onMute(!strip_.muted);
    } else if (soloArea().contains(at)) {
        if (onSolo) onSolo(!strip_.soloed);
    } else if (levelBoxArea().contains(at)) {
        if (onTypeRequest) onTypeRequest(Field::Level);
    } else if (peakBoxArea().contains(at)) {
        resetPeak();
        if (onPeakReset) onPeakReset();
    } else if (panArea().contains(at)) {
        drag_ = Drag::Pan;
        dragPan_ = strip_.linked ? 0.0f : static_cast<float>(*strip_.pan);
        lastMouse_ = e.position.x;
        panPressOnWords_ = e.getNumberOfClicks() == 1 && panTextArea().contains(at);
        panMoved_ = false;
        if (onPanDrag) onPanDrag(juce::roundToInt(dragPan_), true, false);
    } else if (faderArea().contains(at)) {
        drag_ = Drag::Fader;
        const float db = toTenth(currentDb());
        dragFraction_ = fractionForDb(db);
        lastMouse_ = e.position.y;
        if (onLevelDrag) onLevelDrag(db, true, false);
    }
}

void ChannelStrip::mouseDrag(const juce::MouseEvent& e) {
    const float fine = e.mods.isShiftDown() ? kFineDrag : 1.0f;
    if (drag_ == Drag::Fader) {
        dragFraction_ = juce::jlimit(0.0f, 1.0f, dragFraction_ + (lastMouse_ - e.position.y) / travel() * fine);
        lastMouse_ = e.position.y;
        const float db = toTenth(dbAtFraction(dragFraction_));
        if (strip_.db && *strip_.db == db) return;
        strip_.db = db;
        repaint();
        if (onLevelDrag) onLevelDrag(db, false, false);
    } else if (drag_ == Drag::Pan) {
        const float width = static_cast<float>(std::max(1, panArea().getWidth()));
        dragPan_ = juce::jlimit(-100.0f, 100.0f, dragPan_ + (e.position.x - lastMouse_) / width * kPanRange * fine);
        lastMouse_ = e.position.x;
        const int pan = juce::roundToInt(dragPan_);
        if (strip_.linked ? pan == 0 : strip_.pan == pan) return;   // a linked bar unlinks only once it moves off the centre
        panMoved_ = true;
        strip_.linked = false;
        strip_.pan = pan;
        repaint();
        if (onPanDrag) onPanDrag(pan, false, false);
    }
}

void ChannelStrip::mouseUp(const juce::MouseEvent&) {
    const auto ended = drag_;
    drag_ = Drag::None;
    if (ended == Drag::Fader && onLevelDrag) onLevelDrag(toTenth(currentDb()), false, true);
    if (ended == Drag::Pan && onPanDrag) onPanDrag(strip_.linked ? 0 : strip_.pan.value_or(0), false, true);
    if (ended == Drag::Pan && panPressOnWords_ && !panMoved_) {
        // A click on the words: a typed pan, unless a second press comes in time to make it a double-click (the centre).
        const int ticket = ++typeTicket_;
        juce::Timer::callAfterDelay(juce::MouseEvent::getDoubleClickTimeout(), [safe = juce::Component::SafePointer<ChannelStrip>(this), ticket] {
            if (safe != nullptr && safe->typeTicket_ == ticket && safe->onTypeRequest) safe->onTypeRequest(Field::Pan);
        });
    }
    panPressOnWords_ = false;
}

// The second press of a double-click has already started a gesture, which its release ends: the centre or 0 dB is a move
// within it. Called on its own (no press), it is a gesture of its own.
void ChannelStrip::mouseDoubleClick(const juce::MouseEvent& e) {
    ++typeTicket_;
    if (e.mods.isPopupMenu()) return;
    const auto at = e.getPosition();
    if (panArea().contains(at)) {
        if (strip_.linked) return;                                // it follows the song: already where it should be
        strip_.pan = 0;
        dragPan_ = 0.0f;
        repaint();
        if (onPanDrag) onPanDrag(0, drag_ != Drag::Pan, drag_ != Drag::Pan);
    } else if (faderArea().contains(at)) {
        strip_.db = 0.0f;
        dragFraction_ = fractionForDb(0.0f);
        repaint();
        if (onLevelDrag) onLevelDrag(0.0f, drag_ != Drag::Fader, drag_ != Drag::Fader);
    }
}

int ChannelStrip::notchesOf(const juce::MouseWheelDetails& wheel) {
    const float delta = wheel.isReversed ? -wheel.deltaY : wheel.deltaY;
    if (wheel.isSmooth) {
        wheelAccumulated_ += delta;
        const int notches = static_cast<int>(wheelAccumulated_ / kSmoothWheelNotch);
        wheelAccumulated_ -= static_cast<float>(notches) * kSmoothWheelNotch;
        return notches;
    }
    return delta > 0.0f ? 1 : delta < 0.0f ? -1 : 0;
}

void ChannelStrip::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    const auto at = e.getPosition();
    const bool onPan = panArea().contains(at), onZone = zone_.contains(at);
    if (drag_ != Drag::None || (!onPan && !onZone)) {
        Component::mouseWheelMove(e, wheel);                      // the row scrolls
        return;
    }
    const int notches = notchesOf(wheel);
    if (notches == 0) return;
    if (onPan) {
        const int pan = juce::jlimit(-100, 100, (strip_.linked ? 0 : *strip_.pan) + notches);
        if (!strip_.linked && strip_.pan == pan) return;
        strip_.linked = false;
        strip_.pan = pan;
        repaint();
        if (onPanDrag) onPanDrag(pan, true, true);
        return;
    }
    const float db = toTenth(juce::jlimit(kMinDb, kMaxDb, currentDb() + kWheelStepDb * static_cast<float>(notches)));
    if (strip_.db && *strip_.db == db) return;
    strip_.db = db;
    repaint();
    if (onLevelDrag) onLevelDrag(db, true, true);
}

}  // namespace sqlive::ui
