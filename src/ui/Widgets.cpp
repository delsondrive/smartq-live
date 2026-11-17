#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <tuple>

#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/LiveLookAndFeel.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

constexpr float kDisabledAlpha = 0.4f;

int ceilToInt(float x) { return static_cast<int>(std::ceil(x)); }

// The baseline that centres a line of text in that font on a height, as drawText's centring does.
float centredBaseline(const juce::Font& font, float centreY) { return centreY + 0.5f * (font.getAscent() - font.getDescent()); }

// Pieces of text in their own fonts and colours, drawn one after another along a baseline.
struct Run {
    juce::String text;
    juce::Font font;
    juce::Colour colour;
};

float widthOf(const std::vector<Run>& runs) {
    float width = 0.0f;
    for (const auto& run : runs) width += ui::widthOf(run.font, run.text);
    return width;
}

void drawRuns(juce::Graphics& g, const std::vector<Run>& runs, float x, float baseline) {
    for (const auto& run : runs) {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(run.font, run.text, x, baseline);
        g.setColour(run.colour);
        glyphs.draw(g);
        x += ui::widthOf(run.font, run.text);
    }
}

// Paints a disabled control at 40%, as the mockup's .off does.
struct DisabledLayer {
    juce::Graphics& g;
    bool active;
    DisabledLayer(juce::Graphics& graphics, const juce::Component& c) : g(graphics), active(!c.isEnabled()) {
        if (active) g.beginTransparencyLayer(kDisabledAlpha);
    }
    ~DisabledLayer() {
        if (active) g.endTransparencyLayer();
    }
};

juce::Font buttonFont() { return theme::font(theme::sizeBody, true); }
juce::Font chipFont(bool bold) { return theme::font(theme::sizeBody, bold); }
juce::Font pillFont() { return theme::font(theme::sizeSmall); }
juce::Font capsFont() { return theme::tracked(theme::font(theme::sizeCaption, true), 0.8f); }
juce::Font choiceFont() { return theme::font(theme::sizeBody); }
juce::Font segmentFont() { return theme::font(theme::sizeSmall, true); }

}  // namespace

// ---- PillButton ------------------------------------------------------------------------------------------------------

PillButton::PillButton(const juce::String& text, Style style) : juce::Button(text), style_(style) {}

void PillButton::setStyle(Style style) {
    if (style == style_) return;
    style_ = style;
    repaint();
}

int PillButton::idealWidth() const { return ceilToInt(widthOf(buttonFont(), getButtonText()) + 2.0f * static_cast<float>(padding_) + 2.0f); }

void PillButton::setPadding(int pixels) {
    padding_ = std::max(0, pixels);
    repaint();
}

void PillButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const DisabledLayer layer(g, *this);
    const bool hot = highlighted && isEnabled();

    juce::Colour fill = theme::field, border = theme::hairline, ink = hot ? theme::text : theme::mid;
    switch (style_) {
        case Style::Plain: break;
        case Style::Primary: fill = theme::mint; border = juce::Colours::transparentBlack; ink = theme::onAccent; break;
        case Style::Ember: fill = theme::ember; border = juce::Colours::transparentBlack; ink = theme::onAccent; break;
        case Style::Danger: ink = theme::danger; break;
    }
    LiveLookAndFeel::fillButton(g, getLocalBounds().toFloat(), fill, border, hot, down);
    if (style_ == Style::Plain && getToggleState()) {                 // open: the mockup's inset mint line along its foot
        const auto area = getLocalBounds().toFloat();
        juce::Path shape;
        shape.addRoundedRectangle(area, theme::radiusButton);
        g.saveState();
        g.reduceClipRegion(shape);
        g.setColour(theme::mint);
        g.fillRect(area.withTop(area.getBottom() - 2.0f));
        g.restoreState();
        ink = theme::text;
    }

    g.setColour(ink);
    g.setFont(buttonFont());
    g.drawText(getButtonText(), getLocalBounds().reduced(padding_, 0), juce::Justification::centred, true);
}

// ---- Chip ------------------------------------------------------------------------------------------------------------

namespace {

std::vector<Run> chipRuns(const juce::String& label, const juce::String& value, bool labelFirst) {
    const Run valueRun{value, chipFont(true), theme::text};
    if (label.isEmpty()) return {valueRun};
    if (value.isEmpty()) return {{label, chipFont(false), theme::mid}};
    if (labelFirst) return {{label + " ", chipFont(false), theme::mid}, valueRun};
    return {valueRun, {" " + label, chipFont(false), theme::mid}};
}

}  // namespace

Chip::Chip() { setInterceptsMouseClicks(false, false); }

void Chip::setText(const juce::String& label, const juce::String& value, bool labelFirst) {
    if (label == label_ && value == value_ && labelFirst == labelFirst_) return;
    label_ = label;
    value_ = value;
    labelFirst_ = labelFirst;
    repaint();
}

int Chip::idealWidth() const { return ceilToInt(widthOf(chipRuns(label_, value_, labelFirst_)) + 2.0f * 10.0f + 2.0f); }

void Chip::paint(juce::Graphics& g) {
    const auto area = getLocalBounds().toFloat();
    LiveLookAndFeel::fillButton(g, area, theme::field, theme::hairline, false, false);

    const auto runs = chipRuns(label_, value_, labelFirst_);
    const float x = std::max(area.getX() + 10.0f, area.getCentreX() - 0.5f * widthOf(runs));
    g.saveState();
    g.reduceClipRegion(getLocalBounds().reduced(1));
    drawRuns(g, runs, x, centredBaseline(chipFont(false), area.getCentreY()));
    g.restoreState();
}

// ---- StatusPill ------------------------------------------------------------------------------------------------------

StatusPill::StatusPill() { setInterceptsMouseClicks(false, false); }

void StatusPill::set(const juce::String& text, Level level) {
    if (text == text_ && level == level_) return;
    text_ = text;
    level_ = level;
    repaint();
}

int StatusPill::idealWidth() const { return ceilToInt(widthOf(pillFont(), text_) + 2.0f * 8.0f + 2.0f); }

void StatusPill::paint(juce::Graphics& g) {
    const auto area = getLocalBounds().toFloat();
    const auto [accent, tint, edge] = [&] {
        switch (level_) {
            case Level::Warning: return std::tuple{theme::caution, 0.1f, 0.3f};
            case Level::Bad: return std::tuple{theme::danger, 0.15f, 0.4f};
            case Level::Ok: break;
        }
        return std::tuple{theme::mint, 0.1f, 0.25f};
    }();
    const float radius = 0.5f * area.getHeight();
    g.setColour(accent.withAlpha(tint));
    g.fillRoundedRectangle(area, radius);
    g.setColour(accent.withAlpha(edge));
    g.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);

    g.setColour(accent);
    g.setFont(pillFont());
    g.drawText(text_, area.reduced(8.0f, 0.0f), juce::Justification::centred, true);
}

// ---- CapsLabel -------------------------------------------------------------------------------------------------------

CapsLabel::CapsLabel(const juce::String& text) : text_(text.toUpperCase()), colour_(theme::dim) {
    setInterceptsMouseClicks(false, false);
}

void CapsLabel::setText(const juce::String& text) {
    const auto caps = text.toUpperCase();
    if (caps == text_) return;
    text_ = caps;
    repaint();
}

void CapsLabel::setTextColour(juce::Colour colour) {
    if (colour == colour_) return;
    colour_ = colour;
    repaint();
}

int CapsLabel::idealWidth() const { return ceilToInt(widthOf(capsFont(), text_)); }

void CapsLabel::paint(juce::Graphics& g) {
    g.setColour(colour_);
    g.setFont(capsFont());
    g.drawText(text_, getLocalBounds(), juce::Justification::centredLeft, true);
}

// ---- Choice ----------------------------------------------------------------------------------------------------------

namespace {

constexpr float kChoicePadLeft = 8.0f, kChoiceChevronSpace = 22.0f;

}  // namespace

Choice::Choice() = default;

void Choice::setItems(const juce::StringArray& items, int selected) {
    items_ = items;
    selected_ = juce::isPositiveAndBelow(selected, items_.size()) ? selected : -1;
    repaint();
}

void Choice::setSelected(int selected) {
    const int clamped = juce::isPositiveAndBelow(selected, items_.size()) ? selected : -1;
    if (clamped == selected_) return;
    selected_ = clamped;
    repaint();
}

void Choice::setNeedsAttention(bool needs) {
    if (needs == needsAttention_) return;
    needsAttention_ = needs;
    repaint();
}

void Choice::setPlaceholder(const juce::String& text) {
    if (text == placeholder_) return;
    placeholder_ = text;
    repaint();
}

int Choice::idealWidth() const {
    float widest = widthOf(choiceFont(), placeholder_);
    for (const auto& item : items_) widest = std::max(widest, widthOf(choiceFont(), item));
    return ceilToInt(widest + kChoicePadLeft + kChoiceChevronSpace);
}

void Choice::paint(juce::Graphics& g) {
    const DisabledLayer layer(g, *this);
    const auto area = getLocalBounds().toFloat();
    const bool hot = isEnabled() && isMouseOver(true);

    g.setColour(hot ? theme::fieldHover : theme::field);
    g.fillRoundedRectangle(area, theme::radiusField);
    g.setColour(needsAttention_ ? theme::caution : theme::hairline);
    g.drawRoundedRectangle(area.reduced(0.5f), theme::radiusField, 1.0f);

    const auto textArea = area.withTrimmedLeft(kChoicePadLeft).withTrimmedRight(kChoiceChevronSpace);
    if (selected_ >= 0) {
        g.setColour(needsAttention_ ? theme::caution : theme::text);
        g.setFont(choiceFont());
        g.drawText(shorten(items_[selected_], choiceFont(), textArea.getWidth()), textArea, juce::Justification::centredLeft, false);
    } else if (placeholder_.isNotEmpty()) {
        g.setColour(needsAttention_ ? theme::caution : theme::dim);
        g.setFont(choiceFont());
        g.drawText(shorten(placeholder_, choiceFont(), textArea.getWidth()), textArea, juce::Justification::centredLeft, false);
    }
    drawChevron(g, {area.getRight() - 12.0f, area.getCentreY()}, theme::dim);
}

void Choice::mouseDown(const juce::MouseEvent& e) {
    if (!isEnabled() || items_.isEmpty() || e.mods.isPopupMenu()) return;

    juce::PopupMenu menu;
    for (int i = 0; i < items_.size(); ++i) menu.addItem(i + 1, items_[i], true, i == selected_);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(getWidth()),
                       [safe = juce::Component::SafePointer<Choice>(this)](int result) {
                           if (safe != nullptr && result > 0) safe->choose(result - 1);
                       });
}

void Choice::choose(int index) {
    if (index == selected_ || !juce::isPositiveAndBelow(index, items_.size())) return;
    selected_ = index;
    repaint();
    if (onChange) onChange(index);
}

// ---- Segmented -------------------------------------------------------------------------------------------------------

namespace {

constexpr float kSegmentInset = 3.0f, kSegmentGap = 3.0f, kSegmentPad = 9.0f;

}  // namespace

Segmented::Segmented() = default;

void Segmented::setItems(const juce::StringArray& items, int selected) {
    items_ = items;
    selected_ = juce::isPositiveAndBelow(selected, items_.size()) ? selected : -1;
    hovered_ = -1;
    repaint();
}

void Segmented::setSelected(int selected) {
    const int clamped = juce::isPositiveAndBelow(selected, items_.size()) ? selected : -1;
    if (clamped == selected_) return;
    selected_ = clamped;
    repaint();
}

int Segmented::idealWidth() const {
    float width = 2.0f * kSegmentInset + kSegmentGap * static_cast<float>(std::max(0, items_.size() - 1));
    for (const auto& item : items_) width += widthOf(segmentFont(), item) + 2.0f * kSegmentPad;
    return ceilToInt(width);
}

std::vector<juce::Rectangle<float>> Segmented::segments() const {
    std::vector<juce::Rectangle<float>> result;
    if (items_.isEmpty()) return result;

    const auto inner = getLocalBounds().toFloat().reduced(kSegmentInset);
    std::vector<float> widths;
    for (const auto& item : items_) widths.push_back(widthOf(segmentFont(), item) + 2.0f * kSegmentPad);
    const float gaps = kSegmentGap * static_cast<float>(items_.size() - 1);
    const float share = (inner.getWidth() - gaps - std::accumulate(widths.begin(), widths.end(), 0.0f)) / static_cast<float>(items_.size());

    float x = inner.getX();
    for (const float natural : widths) {
        const float width = std::max(0.0f, natural + share);
        result.emplace_back(x, inner.getY(), width, inner.getHeight());
        x += width + kSegmentGap;
    }
    return result;
}

int Segmented::segmentAt(juce::Point<float> point) const {
    const auto all = segments();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].expanded(0.5f * kSegmentGap, kSegmentInset).contains(point)) return static_cast<int>(i);
    return -1;
}

void Segmented::paint(juce::Graphics& g) {
    const DisabledLayer layer(g, *this);
    g.setColour(theme::field);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), theme::radiusButton);

    const auto all = segments();
    g.setFont(segmentFont());
    for (size_t i = 0; i < all.size(); ++i) {
        const int index = static_cast<int>(i);
        if (index == selected_) {
            g.setColour(theme::popup);
            g.fillRoundedRectangle(all[i], theme::radiusField);
        }
        g.setColour(index == selected_ ? theme::text : (index == hovered_ && isEnabled() ? theme::mid : theme::dim));
        g.drawText(items_[index], all[i].reduced(4.0f, 0.0f), juce::Justification::centred, true);
    }
}

void Segmented::mouseDown(const juce::MouseEvent& e) {
    if (!isEnabled() || e.mods.isPopupMenu()) return;
    const int index = segmentAt(e.position);
    if (index < 0 || index == selected_) return;
    selected_ = index;
    repaint();
    if (onChange) onChange(index);
}

void Segmented::mouseMove(const juce::MouseEvent& e) {
    const int index = segmentAt(e.position);
    if (index == hovered_) return;
    hovered_ = index;
    repaint();
}

void Segmented::mouseExit(const juce::MouseEvent&) {
    if (hovered_ < 0) return;
    hovered_ = -1;
    repaint();
}

// ---- Switch ----------------------------------------------------------------------------------------------------------

Switch::Switch() = default;

void Switch::setOn(bool on) {
    if (on == on_) return;
    on_ = on;
    repaint();
}

void Switch::paint(juce::Graphics& g) {
    const DisabledLayer layer(g, *this);
    const auto track = juce::Rectangle<float>(34.0f, 18.0f).withCentre(getLocalBounds().toFloat().getCentre());
    g.setColour(on_ ? theme::mint.withAlpha(0.25f) : theme::field);
    g.fillRoundedRectangle(track, 9.0f);
    g.setColour(on_ ? theme::mint : theme::hairline);
    g.drawRoundedRectangle(track.reduced(0.5f), 9.0f, 1.0f);

    const auto knob = juce::Rectangle<float>(12.0f, 12.0f).withPosition(track.getX() + (on_ ? 19.0f : 3.0f), track.getY() + 3.0f);
    g.setColour(on_ ? theme::mint : theme::dim);
    g.fillEllipse(knob);
}

void Switch::mouseDown(const juce::MouseEvent& e) {
    if (!isEnabled() || e.mods.isPopupMenu()) return;
    on_ = !on_;
    repaint();
    if (onChange) onChange(on_);
}

// ---- LevelFader ------------------------------------------------------------------------------------------------------

namespace {

constexpr float kCapWidth = 32.0f, kCapHeight = 14.0f, kTrackWidth = 8.0f;
constexpr float kWheelStepDb = 0.5f, kFineDrag = 0.25f, kSmoothWheelNotch = 0.1f;

// The travel's three stretches: (fraction at the start, dB at the start), up to the next.
constexpr float kKneeLowFraction = 0.15f, kKneeLowDb = -30.0f, kKneeHighFraction = 0.75f, kKneeHighDb = 0.0f;

}  // namespace

LevelFader::LevelFader() = default;

float LevelFader::dbAtFraction(float fromBottom01) {
    const float f = juce::jlimit(0.0f, 1.0f, fromBottom01);
    if (f <= kKneeLowFraction) return juce::jmap(f, 0.0f, kKneeLowFraction, kMinDb, kKneeLowDb);
    if (f <= kKneeHighFraction) return juce::jmap(f, kKneeLowFraction, kKneeHighFraction, kKneeLowDb, kKneeHighDb);
    return juce::jmap(f, kKneeHighFraction, 1.0f, kKneeHighDb, kMaxDb);
}

float LevelFader::fractionForDb(float db) {
    const float d = juce::jlimit(kMinDb, kMaxDb, db);
    if (d <= kKneeLowDb) return juce::jmap(d, kMinDb, kKneeLowDb, 0.0f, kKneeLowFraction);
    if (d <= kKneeHighDb) return juce::jmap(d, kKneeLowDb, kKneeHighDb, kKneeLowFraction, kKneeHighFraction);
    return juce::jmap(d, kKneeHighDb, kMaxDb, kKneeHighFraction, 1.0f);
}

void LevelFader::setValue(std::optional<float> db) {
    if (db == value_) return;
    value_ = db;
    repaint();
}

void LevelFader::setColour(juce::Colour colour) {
    if (colour == colour_) return;
    colour_ = colour;
    repaint();
}

void LevelFader::setChangedTonight(bool changed) {
    if (changed == changedTonight_) return;
    changedTonight_ = changed;
    repaint();
}

void LevelFader::setMeter(float gain01) {
    const float meter = juce::jlimit(0.0f, 1.0f, gain01);
    if (meter == meter_) return;
    meter_ = meter;
    repaint();
}

float LevelFader::travel() const { return std::max(1.0f, static_cast<float>(getHeight()) - kCapHeight); }

float LevelFader::yForFraction(float fraction) const {
    return static_cast<float>(getHeight()) - 0.5f * kCapHeight - fraction * travel();
}

float LevelFader::currentDb() const { return value_ ? juce::jlimit(kMinDb, kMaxDb, *value_) : kMinDb; }

void LevelFader::paint(juce::Graphics& g) {
    const DisabledLayer layer(g, *this);
    const auto bounds = getLocalBounds().toFloat();
    const float cx = bounds.getCentreX();
    const auto track = juce::Rectangle<float>(cx - 0.5f * kTrackWidth, bounds.getY(), kTrackWidth, bounds.getHeight());
    const float corner = 0.5f * kTrackWidth;

    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.fillRoundedRectangle(track, corner);

    // The meter, from the bottom of the track up to the playing level on the same scale as the cap.
    if (const float meterDb = meter_ > 0.0f ? 20.0f * std::log10(meter_) : kMinDb; meterDb > kMinDb) {
        g.setColour(colour_.withMultipliedAlpha(0.75f));
        g.fillRoundedRectangle(track.withTop(yForFraction(fractionForDb(meterDb))), corner);
    }

    const float capWidth = std::min(kCapWidth, bounds.getWidth());
    const auto cap = juce::Rectangle<float>(capWidth, kCapHeight).withCentre({cx, yForFraction(fractionForDb(currentDb()))});
    juce::Path capShape;
    capShape.addRoundedRectangle(cap, 4.0f);
    juce::DropShadow(juce::Colours::black.withAlpha(0.6f), 6, {0, 2}).drawForPath(g, capShape);
    if (!value_) g.setColour(theme::mid.withAlpha(0.3f));                 // not sent
    else g.setColour(changedTonight_ ? theme::caution : juce::Colour(0xffe9e4ec));
    g.fillPath(capShape);
}

void LevelFader::report(float db, bool start, bool end) {
    if (onDrag) onDrag(db, start, end);
}

void LevelFader::mouseDown(const juce::MouseEvent& e) {
    if (!isEnabled() || e.mods.isPopupMenu()) return;
    dragging_ = true;
    const float db = toTenth(currentDb());
    dragFraction_ = fractionForDb(db);
    lastY_ = e.position.y;
    report(db, true, false);
}

void LevelFader::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_) return;
    const float moved = (lastY_ - e.position.y) / travel() * (e.mods.isShiftDown() ? kFineDrag : 1.0f);
    lastY_ = e.position.y;
    dragFraction_ = juce::jlimit(0.0f, 1.0f, dragFraction_ + moved);

    const float db = toTenth(dbAtFraction(dragFraction_));
    if (value_ && *value_ == db) return;
    value_ = db;
    repaint();
    report(db, false, false);
}

void LevelFader::mouseUp(const juce::MouseEvent&) {
    if (!dragging_) return;
    dragging_ = false;
    report(toTenth(currentDb()), false, true);
}

// The second press of a double-click has already started a gesture, which its release ends: 0 dB is a move within it.
void LevelFader::mouseDoubleClick(const juce::MouseEvent& e) {
    if (!isEnabled() || e.mods.isPopupMenu()) return;
    value_ = 0.0f;
    dragFraction_ = fractionForDb(0.0f);
    repaint();
    report(0.0f, !dragging_, !dragging_);
}

void LevelFader::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (!isEnabled() || dragging_) {
        Component::mouseWheelMove(e, wheel);
        return;
    }
    const float delta = (wheel.isReversed ? -wheel.deltaY : wheel.deltaY);
    int notches = 0;
    if (wheel.isSmooth) {
        wheelAccumulated_ += delta;
        notches = static_cast<int>(wheelAccumulated_ / kSmoothWheelNotch);
        wheelAccumulated_ -= static_cast<float>(notches) * kSmoothWheelNotch;
    } else if (delta != 0.0f) {
        notches = delta > 0.0f ? 1 : -1;
    }
    if (notches == 0) return;

    const float db = toTenth(juce::jlimit(kMinDb, kMaxDb, currentDb() + kWheelStepDb * static_cast<float>(notches)));
    if (value_ && *value_ == db) return;
    value_ = db;
    repaint();
    report(db, true, true);
}

// ---- BeatLights ------------------------------------------------------------------------------------------------------

BeatLights::BeatLights() { setInterceptsMouseClicks(false, false); }

void BeatLights::set(int beatsInBar, int beatNow, float diameter) {
    const int beats = std::max(0, beatsInBar);
    const int now = juce::jlimit(0, beats, beatNow);
    if (beats == beats_ && now == now_ && diameter == diameter_) return;
    beats_ = beats;
    now_ = now;
    diameter_ = diameter;
    repaint();
}

// 5 px between 12 px lights and 10 px between 34 px ones, as the mockup's .beats and .bigbeats.
float BeatLights::gap() const { return std::max(3.0f, 5.0f + (diameter_ - 12.0f) * 5.0f / 22.0f); }

int BeatLights::idealWidth() const {
    return ceilToInt(static_cast<float>(beats_) * diameter_ + static_cast<float>(std::max(0, beats_ - 1)) * gap());
}

void BeatLights::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    const float total = static_cast<float>(beats_) * diameter_ + static_cast<float>(std::max(0, beats_ - 1)) * gap();
    float x = bounds.getCentreX() - 0.5f * total;
    const float glow = 10.0f + (diameter_ - 12.0f) * 14.0f / 22.0f;   // the mockup's 10 px glow, 24 px on the big lights

    for (int beat = 1; beat <= beats_; ++beat, x += diameter_ + gap()) {
        const auto light = juce::Rectangle<float>(x, bounds.getCentreY() - 0.5f * diameter_, diameter_, diameter_);
        if (beat == now_) {
            juce::Path shape;
            shape.addEllipse(light);
            juce::DropShadow(theme::mint.withAlpha(0.9f), juce::roundToInt(glow), {}).drawForPath(g, shape);
            g.setColour(theme::mint);
            g.fillPath(shape);
        } else {
            g.setColour(theme::field);
            g.fillEllipse(light);
            g.setColour(theme::hairline);
            g.drawEllipse(light.reduced(0.5f), 1.0f);
        }
    }
}

// ---- Keys ------------------------------------------------------------------------------------------------------------

float keyWidth(const juce::String& key) { return widthOf(theme::font(theme::sizeCaption, true), key) + 10.0f; }

float drawKey(juce::Graphics& g, const juce::String& key, float x, float centreY) {
    const float w = keyWidth(key);
    const juce::Rectangle<float> box(x, centreY - 9.0f, w, 18.0f);
    g.setColour(theme::field);
    g.fillRoundedRectangle(box, 4.0f);
    g.setColour(theme::hairline);
    g.drawRoundedRectangle(box.reduced(0.5f), 4.0f, 1.0f);
    g.fillRect(box.getX() + 2.0f, box.getBottom() - 1.0f, box.getWidth() - 4.0f, 1.0f);
    g.setColour(theme::text);
    g.setFont(theme::font(theme::sizeCaption, true));
    g.drawText(key, box.withTrimmedBottom(1.0f), juce::Justification::centred, false);
    return w;
}

// ---- Key hints -------------------------------------------------------------------------------------------------------

namespace {

constexpr float kHintKeyGap = 3.0f, kHintWordGap = 4.0f;

// Walks the hint's parts left to right: the word before, the keys, the words.
template <typename Visit>
float walkHint(const KeyHint& hint, const juce::Font& font, Visit visit) {
    float x = 0.0f;
    bool first = true;
    const auto word = [&](const juce::String& text) {
        if (text.isEmpty()) return;
        if (!first) x += kHintWordGap;
        visit(text, false, x);
        x += ui::widthOf(font, text);
        first = false;
    };
    word(hint.before);
    for (int i = 0; i < hint.keys.size(); ++i) {
        if (!first) x += i > 0 ? kHintKeyGap : kHintWordGap;
        visit(hint.keys[i], true, x);
        x += keyWidth(hint.keys[i]);
        first = false;
    }
    word(hint.words);
    return x;
}

}  // namespace

juce::String KeyHint::text() const {
    juce::StringArray parts{before, keys.joinIntoString(" "), words};
    parts.removeEmptyStrings();
    return parts.joinIntoString(" ");
}

juce::StringArray textsOf(const std::vector<KeyHint>& hints) {
    juce::StringArray texts;
    for (const auto& hint : hints) texts.add(hint.text());
    return texts;
}

float hintWidth(const KeyHint& hint, const juce::Font& font) {
    return walkHint(hint, font, [](const juce::String&, bool, float) {});
}

void drawHint(juce::Graphics& g, const KeyHint& hint, const juce::Font& font, float x, float centreY) {
    const float height = std::ceil(font.getHeight()) + 4.0f;
    walkHint(hint, font, [&](const juce::String& text, bool key, float at) {
        if (key) {
            drawKey(g, text, x + at, centreY);
            return;
        }
        g.setFont(font);
        g.setColour(theme::dim);
        g.drawText(text, juce::Rectangle<float>(x + at, centreY - 0.5f * height, ui::widthOf(font, text) + 1.0f, height),
                   juce::Justification::centredLeft, false);
    });
}

}  // namespace sqlive::ui
