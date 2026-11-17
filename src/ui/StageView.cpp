#include "ui/StageView.h"

#include <algorithm>
#include <cmath>

#include "app/ShowController.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's CSS, for a 1280 x 698 area: .stage (padding 24 32, gap 22), .nowrow (.lbl 15 px bold, letter-spaced 2;
// .title 72 px semibold, line-height 1.05), .big (gap 32; b 76 px light, line-height 1; the beat 34 px in dim; small
// 14 px, letter-spaced 1, in dim), .bigbeats (34 px lights, 20 px to the progress bar), .prog (10 px, 5 px corners),
// .nextbox (2 px dashed ember, 14 px corners, padding 16 22; .lbl 14 px bold; .title 40 px semibold; .info 18 px in mid),
// .cta (18 px corners, 2 px border, padding 30, gap 26; .bigkey 36 px bold, padding 16 36, 12 px corners; b 40 px
// semibold; small 17 px in mid; .pill 24 px bold, letter-spaced 1; the loading bar 8 px below, 520 px at most),
// .after (gap 8; 10 px corners, padding 12 14, 16 px semibold; b in mid, 6 px to the title), .keysbar (14 px in dim,
// gap 18). Line heights are the browser's 1.4 for these fonts.
constexpr float kDesignWidth = 1280.0f, kDesignHeight = 698.0f;
constexpr float kPadX = 32.0f, kPadY = 24.0f, kGap = 22.0f;
constexpr float kLabelLine = 21.0f, kTitleLine = 1.05f, kTitleMax = 72.0f, kTitleMin = 44.0f, kTitleGap = 32.0f;
constexpr float kFigureSize = 76.0f, kFigureSmall = 34.0f, kFigureCaption = 20.0f, kFigureGap = 32.0f;
constexpr float kLight = 34.0f, kLightsToBar = 20.0f, kBarHeight = 10.0f, kRingX = 12.0f, kRingY = 10.0f, kRingStroke = 3.0f;
constexpr float kNextHeight = 112.0f, kEndHeight = 92.0f, kNextPadX = 22.0f, kNextPadY = 16.0f, kNextGap = 16.0f, kBoxRadius = 14.0f;
constexpr float kNextLabelLine = 20.0f, kNextTitleLine = 56.0f, kInfoLine = 25.0f;
constexpr float kCtaHeight = 162.0f, kCtaRadius = 18.0f, kCtaPad = 30.0f, kCtaGap = 26.0f, kKeyPadX = 36.0f, kKeyHeight = 80.0f;
constexpr float kActionLine = 56.0f, kDetailLine = 24.0f, kLoadGap = 8.0f, kLoadWidth = 520.0f;
constexpr float kAfterHeight = 46.0f, kAfterGap = 8.0f, kAfterPadX = 14.0f, kAfterNumberGap = 6.0f;
constexpr float kSentenceLine = 42.0f, kHintLine = 20.0f, kHintGap = 18.0f;

// The mockup's weight 300, for the big figures.
juce::Font lightFont(float size) {
#if JUCE_WINDOWS
    return juce::Font(juce::FontOptions(theme::typefaceName(), "Light", size).withPointHeight(size));
#else
    return theme::font(size);
#endif
}

juce::Font labelFont() { return theme::tracked(theme::font(15.0f, true), 2.0f); }
juce::Font titleFont(float size) { return theme::font(size, true); }
juce::Font captionFont() { return theme::tracked(theme::font(14.0f), 1.0f); }
juce::Font nextLabelFont() { return theme::tracked(theme::font(14.0f, true), 2.0f); }
juce::Font nextTitleFont() { return theme::font(40.0f, true); }
juce::Font infoFont() { return theme::font(18.0f); }
juce::Font holdFont() { return theme::font(24.0f, true); }
juce::Font keyFont() { return theme::font(36.0f, true); }
juce::Font actionFont() { return theme::font(40.0f, true); }
juce::Font detailFont() { return theme::font(17.0f); }
juce::Font pillFont() { return theme::tracked(theme::font(24.0f, true), 1.0f); }
juce::Font afterFont() { return theme::font(16.0f, true); }
juce::Font sentenceFont() { return theme::font(30.0f); }
juce::Font hintFont() { return theme::font(14.0f); }

// Every digit as a 0: the figure's width while it counts (the UI font's digits are all as wide).
juce::String asTemplate(const juce::String& text) {
    juce::String out;
    for (auto c : text) out << (juce::CharacterFunctions::isDigit(c) ? juce::juce_wchar('0') : c);
    return out;
}

// The first stretch of a path that long.
juce::Path partOf(const juce::Path& path, float fraction) {
    const float wanted = path.getLength() * std::clamp(fraction, 0.0f, 1.0f);
    juce::Path part;
    juce::PathFlatteningIterator it(path);
    float done = 0.0f;
    bool started = false;
    while (done < wanted && it.next()) {
        const juce::Line<float> line(it.x1, it.y1, it.x2, it.y2);
        if (!started) {
            part.startNewSubPath(line.getStart());
            started = true;
        }
        const float length = line.getLength();
        if (done + length >= wanted) {
            part.lineTo(line.getPointAlongLine(wanted - done));
            break;
        }
        part.lineTo(line.getEnd());
        done += length;
    }
    return part;
}

void drawDashedBox(juce::Graphics& g, juce::Rectangle<float> box, float radius, float thickness, juce::Colour colour) {
    juce::Path outline, dashed;
    outline.addRoundedRectangle(box.reduced(0.5f * thickness), radius);
    const float dashes[] = {6.0f, 4.0f};
    juce::PathStrokeType(thickness).createDashedStroke(dashed, outline, dashes, 2);
    g.setColour(colour);
    g.fillPath(dashed);
}

void drawBox(juce::Graphics& g, juce::Rectangle<float> box, float radius, float thickness, juce::Colour fill, juce::Colour border) {
    g.setColour(fill);
    g.fillRoundedRectangle(box, radius);
    g.setColour(border);
    g.drawRoundedRectangle(box.reduced(0.5f * thickness), radius, thickness);
}

// Text in a box, shortened with "…" to its width. Returns the words drawn.
juce::String drawFitted(juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour, juce::Rectangle<float> box,
                        juce::Justification justification = juce::Justification::centredLeft) {
    g.setFont(font);
    g.setColour(colour);
    auto shown = shorten(text, font, box.getWidth());
    g.drawText(shown, box, justification, false);
    return shown;
}

}  // namespace

StageView::StageView(app::Session& session, app::ShowController* controller) : session_(session), controller_(controller) {
    addChildComponent(beats_);
    // Nothing here takes the keyboard, not even with a click: Space and the arrows must reach the window.
    for (auto* c : {static_cast<juce::Component*>(this), static_cast<juce::Component*>(&beats_)}) {
        c->setWantsKeyboardFocus(false);
        c->setMouseClickGrabsKeyboardFocus(false);
    }
    session_.changes().addChangeListener(this);
    refresh();
}

StageView::~StageView() {
    stopTimer();
    session_.changes().removeChangeListener(this);
}

void StageView::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(); }

// While a song plays the session's changes come 30 times a second as well; a tick just after one of them has nothing new.
void StageView::timerCallback() {
    if (juce::Time::getMillisecondCounterHiRes() - lastRefreshMs_ >= 20.0) refresh();
}

void StageView::visibilityChanged() {
    if (isVisible() && !fresh_) refresh();
}

std::vector<StageView::Figure> StageView::figuresOf(const ShowState& st) const {
    if (st.playing) return {{st.bar, st.beat, "BAR"}, {st.bpm, {}, st.bpmLabel}, {st.left, {}, "LEFT"}};
    if (st.interrupted) return {{st.bar, {}, "BAR"}};               // the mockup's screen 5: where the song was cut off
    if (st.upTitle.isNotEmpty() || st.upBpm.isNotEmpty()) {
        std::vector<Figure> figures{{st.upBpm, {}, st.upBpmLabel}};
        if (st.upKey.isNotEmpty()) figures.push_back({st.upKey, {}, "KEY"});   // no key: no figure, as the top bar has no chip
        figures.push_back({st.upLength, {}, "LENGTH"});
        return figures;
    }
    return {};
}

bool StageView::labelIsNow() const { return shape_.screen == Screen::Playing || (shape_.screen == Screen::Between && state_.interrupted); }

juce::String StageView::drawnLabel() const { return labelIsNow() ? state_.label : state_.upLabel; }

StageView::Shape StageView::shapeOf(const ShowState& st) const {
    Shape shape;
    shape.screen = st.playing ? Screen::Playing : (st.endOfSet ? Screen::Ended : Screen::Between);
    shape.title = st.playing ? st.title : (st.endOfSet ? st.setlistName : st.upTitle);
    for (const auto& figure : figuresOf(st)) {
        const bool counts = shape.screen == Screen::Playing;
        shape.figures.add(counts ? asTemplate(figure.number) : figure.number);
        shape.figures.add(counts ? asTemplate(figure.small) : figure.small);
        shape.figures.add(figure.caption);
    }
    shape.beats = st.playing ? st.beatsInBar : 0;
    shape.endOfSet = st.endOfSet;
    shape.noSongs = st.cards.empty();
    if (!st.interrupted) shape.hints = st.hints;                 // cut off: the shell's box over the bottom says the keys
    return shape;
}

void StageView::refresh() {
    if (!shownWithParents(*this)) {                               // hidden: it catches up when shown
        fresh_ = false;
        stopTimer();
        return;
    }
    fresh_ = true;
    lastRefreshMs_ = juce::Time::getMillisecondCounterHiRes();
    auto state = makeShowState(session_);
    const double hold = controller_ != nullptr && state.playing ? controller_->holdProgress() : 0.0;
    const auto shape = shapeOf(state);

    if (!laidOut_ || !(shape == shape_)) {
        state_ = std::move(state);
        hold_ = hold;
        shape_ = shape;
        layout();
        repaintAll();
    } else {
        repaintChanges(state, hold);
        state_ = std::move(state);
        hold_ = hold;
        if (shape_.screen == Screen::Playing) beats_.set(state_.beatsInBar, state_.beatNow, kLight * scale_);
    }

    if (state_.playing && !isTimerRunning()) startTimerHz(30);
    else if (!state_.playing && isTimerRunning()) stopTimer();
}

juce::Rectangle<int> StageView::areaOf(juce::Rectangle<float> area) const {
    return (area * scale_).getSmallestIntegerContainer().expanded(2);
}

void StageView::repaintDesign(juce::Rectangle<float> area) {
    const auto pixels = areaOf(area);
    ++repaints_asked_;
    repainted_ = repainted_.isEmpty() ? pixels : repainted_.getUnion(pixels);
    repaint(pixels);
}

void StageView::repaintAll() {
    ++repaints_asked_;
    repainted_ = getLocalBounds();
    repaint();
}

int StageView::progressPixels(double progress) const {
    return juce::roundToInt(std::clamp(progress, 0.0, 1.0) * bar_.getWidth() * scale_);
}

void StageView::repaintChanges(const ShowState& st, double hold) {
    const auto& was = state_;
    switch (shape_.screen) {
    case Screen::Playing:
        if (st.label != was.label) repaintDesign(labelBox_);
        if (st.bar != was.bar || st.beat != was.beat || st.bpm != was.bpm || st.bpmLabel != was.bpmLabel || st.left != was.left)
            repaintDesign(figuresBox_);
        if ((hold > 0.0) != (hold_ > 0.0) || progressPixels(st.songProgress) != progressPixels(was.songProgress)) repaintDesign(barRow_);
        if (hold != hold_) repaintDesign(ring_.expanded(kRingStroke));
        if (st.nextLabel != was.nextLabel || st.nextTitle != was.nextTitle || st.nextInfo != was.nextInfo || st.readiness != was.readiness)
            repaintDesign(next_);
        break;
    case Screen::Between:
        if (st.upLabel != was.upLabel || st.label != was.label) repaintDesign(labelBox_);
        if (st.action != was.action || st.actionDetail != was.actionDetail || st.pill != was.pill || st.readiness != was.readiness
            || st.loadProgress != was.loadProgress || st.startKey != was.startKey)
            repaintDesign(cta_);
        if (st.after != was.after) repaintDesign(after_);
        break;
    case Screen::Ended:
        break;
    }
}

void StageView::resized() { layout(); }

void StageView::layout() {
    ++layouts_done_;
    laidOut_ = true;
    const float w = static_cast<float>(getWidth()), h = static_cast<float>(getHeight());
    scale_ = w > 0.0f && h > 0.0f ? std::min(w / kDesignWidth, h / kDesignHeight) : 1.0f;
    const float width = w / scale_, height = h / scale_;
    const float left = kPadX, right = std::max(left, width - kPadX), inner = right - left;
    float y = kPadY;

    // The top row: the label and the title on the left, the figures on the right, their bottoms in line.
    const auto figures = figuresOf(state_);
    figureColumns_.clear();
    float x = right;
    for (auto it = figures.rbegin(); it != figures.rend(); ++it) {
        const float number = widthOf(lightFont(kFigureSize), asTemplate(it->number)) + widthOf(lightFont(kFigureSmall), asTemplate(it->small));
        const float columnWidth = std::ceil(std::max(number, widthOf(captionFont(), it->caption)));
        x -= columnWidth;
        figureColumns_.insert(figureColumns_.begin(), {x, y + kLabelLine + kTitleMax * kTitleLine - kFigureSize - kFigureCaption, columnWidth,
                                                       kFigureSize + kFigureCaption});
        x -= kFigureGap;
    }
    figuresBox_ = {};
    for (const auto& column : figureColumns_) figuresBox_ = figuresBox_.isEmpty() ? column : figuresBox_.getUnion(column);
    const float titleRight = figureColumns_.empty() ? right : std::max(left, figuresBox_.getX() - kTitleGap);
    labelBox_ = {left, y, titleRight - left, kLabelLine};
    titleBox_ = {left, y + kLabelLine, titleRight - left, kTitleMax * kTitleLine};
    fitTitle(shape_.title, titleBox_.getWidth());
    y = titleBox_.getBottom() + kGap;

    // While playing: the beat lights and the progress bar, then the next song.
    beats_.setVisible(shape_.screen == Screen::Playing);
    if (shape_.screen == Screen::Playing) {
        beats_.set(state_.beatsInBar, state_.beatNow, kLight * scale_);
        lights_ = {left, y, static_cast<float>(beats_.idealWidth()) / scale_, kLight};
        ring_ = lights_.expanded(kRingX, kRingY);
        barRow_ = {lights_.getRight() + kLightsToBar, y, std::max(0.0f, right - lights_.getRight() - kLightsToBar), kLight};
        bar_ = barRow_.withSizeKeepingCentre(barRow_.getWidth(), kBarHeight);
        const float glow = (10.0f + (kLight * scale_ - 12.0f) * 14.0f / 22.0f) / scale_ + 2.0f;   // BeatLights' glow, and a little
        beats_.setBounds((lights_.expanded(glow) * scale_).getSmallestIntegerContainer().getIntersection(getLocalBounds()));
        y += kLight + kGap;
        next_ = {left, y, inner, shape_.endOfSet ? kEndHeight : kNextHeight};
    } else {
        lights_ = ring_ = bar_ = barRow_ = next_ = {};
    }

    // Between songs: the call to action, then the row of songs.
    if (shape_.screen == Screen::Between) {
        cta_ = {left, y, inner, kCtaHeight};
        after_ = {left, cta_.getBottom() + kGap, inner, kAfterHeight};
    } else {
        cta_ = after_ = {};
    }
    sentence_ = shape_.screen == Screen::Ended ? juce::Rectangle<float>(left, y, inner, kSentenceLine) : juce::Rectangle<float>();

    // The hints along the bottom, as many to a line as fit.
    hintLines_.clear();
    float used = 0.0f;
    for (int i = 0; i < shape_.hints.size(); ++i) {
        const float hint = hintWidth(shape_.hints[i], hintFont());
        if (hintLines_.empty() || used + kHintGap + hint > inner) {
            hintLines_.push_back({i});
            used = hint;
        } else {
            hintLines_.back().push_back(i);
            used += kHintGap + hint;
        }
    }
    const float hintsHeight = hintLines_.empty() ? 0.0f : hintLines_.size() * kHintLine + (hintLines_.size() - 1) * kHintGap;
    hintsBox_ = {left, height - kPadY - hintsHeight, inner, hintsHeight};
}

// 72 px if it fits; else as much smaller as it needs, down to 44 px; then shortened with "…".
void StageView::fitTitle(const juce::String& text, float width) {
    float size = kTitleMax;
    const float full = widthOf(titleFont(size), text);
    if (full > width && full > 0.0f) {
        size = std::clamp(std::floor(kTitleMax * width / full), kTitleMin, kTitleMax);
        while (size > kTitleMin && widthOf(titleFont(size), text) > width) size -= 1.0f;
    }
    titleSize_ = size;
    title_ = shorten(text, titleFont(size), width);
}

void StageView::paint(juce::Graphics& g) {
    g.addTransform(juce::AffineTransform::scale(scale_));
    paintHeader(g);
    switch (shape_.screen) {
    case Screen::Playing: paintPlaying(g); break;
    case Screen::Between: paintBetween(g); break;
    case Screen::Ended: paintEnded(g); break;
    }
    paintHints(g);
}

// The label, the title and the figures.
void StageView::paintHeader(juce::Graphics& g) {
    const auto clip = g.getClipBounds().toFloat();
    if (clip.intersects(labelBox_)) drawFitted(g, drawnLabel(), labelFont(), labelIsNow() ? theme::mint : theme::ember, labelBox_);
    if (clip.intersects(titleBox_)) {
        const float line = titleSize_ * kTitleLine;
        g.setFont(titleFont(titleSize_));
        g.setColour(theme::text);
        g.drawText(title_, titleBox_.withTop(titleBox_.getBottom() - line), juce::Justification::centredLeft, false);
    }
    if (!clip.intersects(figuresBox_)) return;
    const auto figures = figuresOf(state_);
    for (std::size_t i = 0; i < figures.size() && i < figureColumns_.size(); ++i) {
        const auto& figure = figures[i];
        auto column = figureColumns_[i];
        const auto numberBox = column.removeFromTop(kFigureSize);
        const auto big = lightFont(kFigureSize), small = lightFont(kFigureSmall);
        const float smallWidth = widthOf(small, figure.small);
        const float bigWidth = widthOf(big, figure.number);
        const float baseline = numberBox.getY() + 0.5f * (kFigureSize + big.getAscent() - big.getDescent());
        const float x = column.getRight() - bigWidth - smallWidth;
        g.setColour(theme::text);
        g.setFont(big);
        g.drawSingleLineText(figure.number, juce::roundToInt(x), juce::roundToInt(baseline));
        if (figure.small.isNotEmpty()) {
            g.setColour(theme::dim);
            g.setFont(small);
            g.drawSingleLineText(figure.small, juce::roundToInt(x + bigWidth), juce::roundToInt(baseline));
        }
        drawFitted(g, figure.caption, captionFont(), theme::dim, column, juce::Justification::centredRight);
    }
}

void StageView::paintPlaying(juce::Graphics& g) {
    const auto clip = g.getClipBounds().toFloat();

    // The ring that fills while Space is held to stop.
    if (hold_ > 0.0 && clip.intersects(ring_.expanded(kRingStroke))) {
        juce::Path ring;
        ring.addRoundedRectangle(ring_, 0.5f * ring_.getHeight());
        const juce::PathStrokeType stroke(kRingStroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour(theme::hairline);
        g.strokePath(ring, stroke);
        g.setColour(theme::text);
        g.strokePath(partOf(ring, static_cast<float>(hold_)), stroke);
    }

    // The song's progress, or "Keep holding to stop".
    if (clip.intersects(barRow_)) {
        if (hold_ > 0.0) {
            drawFitted(g, "Keep holding to stop", holdFont(), theme::text, barRow_);
        } else {
            const float radius = 0.5f * kBarHeight;
            g.setColour(theme::field);
            g.fillRoundedRectangle(bar_, radius);
            const float filled = static_cast<float>(progressPixels(state_.songProgress)) / scale_;
            if (filled > 0.0f) {
                juce::Graphics::ScopedSaveState saved(g);
                juce::Path track;
                track.addRoundedRectangle(bar_, radius);
                g.reduceClipRegion(track);
                g.setColour(theme::mint);
                g.fillRect(bar_.withWidth(filled));
            }
        }
    }

    // The next song: "NEXT SONG · ✓ READY", its title, then on the right its BPM and key and how it follows.
    if (!clip.intersects(next_)) return;
    if (shape_.endOfSet) {
        drawBox(g, next_, kBoxRadius, 2.0f, juce::Colours::transparentBlack, theme::hairline);
        drawFitted(g, state_.nextLabel, nextTitleFont(), theme::mid, next_.reduced(kNextPadX, kNextPadY));
        return;
    }
    const bool failed = state_.readiness == Readiness::Failed;
    drawDashedBox(g, next_, kBoxRadius, 2.0f, failed ? theme::danger : theme::ember);
    auto inner = next_.reduced(kNextPadX + 2.0f, kNextPadY + 2.0f);
    const auto info = juce::StringArray::fromLines(state_.nextInfo);
    float infoWidth = 0.0f;
    for (const auto& line : info) infoWidth = std::max(infoWidth, widthOf(infoFont(), line));
    infoWidth = std::min(infoWidth, 0.5f * inner.getWidth());
    auto infoBox = inner.removeFromRight(infoWidth);
    inner.removeFromRight(kNextGap);
    infoBox = infoBox.withSizeKeepingCentre(infoBox.getWidth(), kInfoLine * static_cast<float>(info.size()));
    for (const auto& line : info) drawFitted(g, line, infoFont(), theme::mid, infoBox.removeFromTop(kInfoLine), juce::Justification::centredRight);
    inner = inner.withSizeKeepingCentre(inner.getWidth(), kNextLabelLine + kNextTitleLine);
    drawFitted(g, state_.nextLabel, nextLabelFont(), failed ? theme::danger : theme::ember, inner.removeFromTop(kNextLabelLine));
    drawFitted(g, state_.nextTitle, nextTitleFont(), theme::text, inner);
}

void StageView::paintBetween(juce::Graphics& g) {
    // A song cut off: nothing can play until it is started again, and the shell's WHEN IT COMES BACK box says how, so
    // no call to action (its mint means ready) and no row of songs; only the label, the title and the bar it reached.
    if (state_.interrupted) {
        drawnCallToAction_.clear();
        return;
    }
    const auto clip = g.getClipBounds().toFloat();

    // The call to action: mint when Start's key will start the song, grey while it loads, red when it can't play.
    if (clip.intersects(cta_)) {
        const auto readiness = state_.readiness;
        const bool ready = readiness == Readiness::Ready, failed = readiness == Readiness::Failed, loading = !ready && !failed;
        const auto fill = ready ? theme::mint.withAlpha(0.1f) : failed ? theme::danger.withAlpha(0.1f) : theme::field;
        const auto border = ready ? theme::mint : failed ? theme::danger : theme::hairline;
        drawBox(g, cta_, kCtaRadius, 2.0f, fill, border);
        auto inner = cta_.reduced(kCtaPad + 2.0f);

        // Start's key (or pedal), as wide as its word, up to a third of the panel.
        const float keyWidth =
            std::min(std::ceil(widthOf(keyFont(), state_.startKey)) + 2.0f * kKeyPadX, std::max(0.0f, inner.getWidth() / 3.0f));
        const auto key = inner.removeFromLeft(keyWidth).withSizeKeepingCentre(keyWidth, kKeyHeight);
        g.setColour(ready ? theme::mint : theme::field);
        g.fillRoundedRectangle(key, 12.0f);
        drawnStartKey_ = drawFitted(g, state_.startKey, keyFont(), ready ? theme::onAccent : theme::dim, key.reduced(kKeyPadX / 2.0f, 0.0f),
                                    juce::Justification::centred);
        inner.removeFromLeft(kCtaGap);

        const float pillWidth = std::min(std::ceil(widthOf(pillFont(), state_.pill)) + 2.0f, std::max(0.0f, 0.4f * inner.getWidth()));
        const auto pill = drawFitted(g, state_.pill, pillFont(), ready ? theme::mint : failed ? theme::danger : theme::dim, inner.removeFromRight(pillWidth),
                   juce::Justification::centredRight);
        inner.removeFromRight(kCtaGap);

        auto words = inner.withSizeKeepingCentre(inner.getWidth(), kActionLine + kDetailLine + (loading ? kLoadGap + kBarHeight : 0.0f));
        const auto action = drawFitted(g, state_.action, actionFont(), loading ? theme::mid : theme::text, words.removeFromTop(kActionLine));
        const auto detail = drawFitted(g, state_.actionDetail, detailFont(), theme::mid, words.removeFromTop(kDetailLine));
        drawnCallToAction_ = {action, detail, pill};
        if (loading) {
            words.removeFromTop(kLoadGap);
            const auto track = words.removeFromTop(kBarHeight).withWidth(std::min(kLoadWidth, words.getWidth()));
            g.setColour(theme::field);
            g.fillRoundedRectangle(track, 0.5f * kBarHeight);
            const float filled = track.getWidth() * static_cast<float>(std::clamp(state_.loadProgress, 0.0, 1.0));
            if (filled > 0.0f) {
                g.setColour(theme::dim);
                g.fillRoundedRectangle(track.withWidth(std::max(filled, kBarHeight)), 0.5f * kBarHeight);
            }
        }
    }

    // The row of songs: the one just played, the next (dashed in ember), and those after it.
    if (!clip.intersects(after_) || state_.after.empty()) return;
    using Kind = ShowState::After::Kind;
    const float count = static_cast<float>(state_.after.size());
    const float itemWidth = (after_.getWidth() - (count - 1.0f) * kAfterGap) / count;
    float x = after_.getX();
    const auto tick = utf8("\xe2\x9c\x93 ");                       // "✓ "
    for (const auto& item : state_.after) {
        const juce::Rectangle<float> box(x, after_.getY(), std::max(0.0f, itemWidth), after_.getHeight());
        x += itemWidth + kAfterGap;
        const bool played = item.kind == Kind::Played, next = item.kind == Kind::Next;
        if (played) g.beginTransparencyLayer(0.6f);
        g.setColour(theme::field);
        g.fillRoundedRectangle(box, theme::radiusPanel);
        if (next) drawDashedBox(g, box, theme::radiusPanel, 1.0f, theme::ember);

        // "✓ 3 Hold The Line · played": the tick, the number in mid, then the rest.
        auto text = box.reduced(kAfterPadX, 0.0f);
        auto rest = item.text;
        const auto font = afterFont();
        if (played && rest.startsWith(tick)) {
            drawFitted(g, tick, font, theme::dim, text);
            text.removeFromLeft(std::min(text.getWidth(), widthOf(font, tick)));
            rest = rest.substring(tick.length());
        }
        const auto number = rest.upToFirstOccurrenceOf(" ", false, false);
        rest = rest.fromFirstOccurrenceOf(" ", false, false);
        const float numberWidth = std::min(text.getWidth(), widthOf(font, number));
        drawFitted(g, number, font, theme::mid, text);
        text.removeFromLeft(std::min(text.getWidth(), numberWidth + kAfterNumberGap));
        drawFitted(g, rest, font, next ? theme::text : theme::dim, text);
        if (played) g.endTransparencyLayer();
    }
}

// The last song has ended: the setlist's name, and under it that it was the last song.
void StageView::paintEnded(juce::Graphics& g) {
    if (!g.getClipBounds().toFloat().intersects(sentence_)) return;
    drawFitted(g, shape_.noSongs ? "No songs yet" : "That was the last song.", sentenceFont(), theme::mid, sentence_);
}

void StageView::paintHints(juce::Graphics& g) {
    if (!g.getClipBounds().toFloat().intersects(hintsBox_)) return;
    float centreY = hintsBox_.getY() + 0.5f * kHintLine;
    for (const auto& line : hintLines_) {
        float x = hintsBox_.getX();
        for (const int i : line) {
            const auto& hint = shape_.hints[i];
            drawHint(g, hint, hintFont(), x, centreY);
            x += hintWidth(hint, hintFont()) + kHintGap;
        }
        centreY += kHintLine + kHintGap;
    }
}

}  // namespace sqlive::ui
