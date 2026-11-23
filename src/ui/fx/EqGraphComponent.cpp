#include "ui/fx/EqGraphComponent.h"

#include <algorithm>
#include <cmath>

// Copied from Smart Q (smarteq_vst src/ui/EqGraphComponent.cpp): see EqGraphComponent.h.

#include "dsp/FilterDesign.h"
#include "ui/fx/ChainHost.h"
#include "ui/fx/FrequencyGrid.h"
#include "ui/fx/LayerImage.h"
#include "ui/fx/PageLook.h"
#include "ui/fx/Theme.h"

namespace smartq {

namespace {

constexpr double kCurveLimitDb = 36.0;  // Curves are drawn just past the graph's edge, not to -300 dB.
constexpr int kDragThresholdPixels = 3;  // so that a double-click can't nudge a handle
constexpr double kWheelGestureSeconds = 0.3;  // a pause that ends one scroll as far as the host goes
constexpr double kNoticeSeconds = 2.5;
constexpr double kNoticeFadeSeconds = 0.4;

constexpr float kHandleRadius = 9.0f;
constexpr double kMotionSeconds = 0.07;      // how quickly a handle takes on a new state
constexpr float kMotionSettled = 0.01f;
constexpr float kOffCurveOpacity = 0.28f;  // a band switched off: its own curve, faint
constexpr float kOffHandleOpacity = 0.55f;  // and its handle, a ring with its number

size_t idx(int n) { return static_cast<size_t>(n); }

void addCurveTo(juce::Path& path, const std::vector<CurvePoint>& curve, const GraphMapping& mapping) {
    bool first = true;
    for (const auto& point : curve) {
        const double db = juce::jlimit(-kCurveLimitDb, kCurveLimitDb, static_cast<double>(point.db));
        const juce::Point<float> at{mapping.freqToX(point.hz), mapping.dbToY(db)};
        if (first)
            path.startNewSubPath(at);
        else
            path.lineTo(at);
        first = false;
    }
}

// The same, leaving out stretches where the band does nothing, so that 24 flat tails don't tint the 0 dB line.
void addActivePartsTo(juce::Path& path, const std::vector<CurvePoint>& curve, const GraphMapping& mapping) {
    constexpr float kFlatDb = 0.15f;
    const auto count = static_cast<int>(curve.size());
    const auto active = [&](int i) { return i >= 0 && i < count && std::abs(curve[idx(i)].db) > kFlatDb; };
    const auto shown = [&](int i) { return active(i - 1) || active(i) || active(i + 1); };

    for (int i = 0; i < count; ++i) {
        if (!shown(i)) continue;
        const auto& point = curve[idx(i)];
        const double db = juce::jlimit(-kCurveLimitDb, kCurveLimitDb, static_cast<double>(point.db));
        const juce::Point<float> at{mapping.freqToX(point.hz), mapping.dbToY(db)};
        if (i > 0 && shown(i - 1))
            path.lineTo(at);
        else
            path.startNewSubPath(at);
    }
}

// A fill for the area between a curve and the 0 dB line, strongest at the line and gone by about 24 dB, so
// that a deep cut reads as a shape rather than as a block of colour.
juce::ColourGradient fadingFromZero(const GraphMapping& mapping, juce::Colour colour, float alpha) {
    juce::ColourGradient gradient(colour.withAlpha(0.0f), 0.0f, mapping.dbToY(24.0), colour.withAlpha(0.0f), 0.0f, mapping.dbToY(-24.0), false);
    gradient.addColour(0.5, colour.withAlpha(alpha));
    return gradient;
}

juce::String channelLetter(dsp::ChannelMode channel) {
    switch (channel) {
        case dsp::ChannelMode::Left: return "L";
        case dsp::ChannelMode::Right: return "R";
        case dsp::ChannelMode::Mid: return "M";
        case dsp::ChannelMode::Side: return "S";
        case dsp::ChannelMode::Stereo: break;
    }
    return {};
}

}  // namespace

EqGraphComponent::EqGraphComponent(ChainHost& processor, int eq)
    : processor_(processor), eq_(eq), audioIdle_(processor.audioBlocksProcessed()), gesture_(processor.apvts(), eq) {
    setOpaque(true);
    for (int band = 0; band < dsp::kMaxBands; ++band)
        params_[idx(band)] = lookupBandParams(processor_.apvts(), eq_, band);
    bypassParam_ = processor_.apvts().getRawParameterValue(kBypassId);
    eqBypassParam_ = processor_.apvts().getRawParameterValue(moduleParamId(eq_, kBypassId));
    bypassed_ = bypassParam_->load() >= 0.5f || eqBypassParam_->load() >= 0.5f;
    readBands();
    updateLiveBands();
    curves_.update(liveBands_, curveSampleRate());
    if (anyDynamic_) staticCurves_.update(bands_, curveSampleRate());
    selected_ = nextSelection(-1, enabled_);

    // A window opens with its handles already in place.
    for (size_t band = 0; band < motion_.size(); ++band) {
        motion_[band].presence = enabled_[band] ? 1.0f : 0.0f;
        motion_[band].selected = static_cast<int>(band) == selected_ ? 1.0f : 0.0f;
    }
}

double EqGraphComponent::curveSampleRate() const {
    const double rate = processor_.getSampleRate();
    return rate > 0.0 ? rate : kFallbackSampleRate;
}

GraphMapping EqGraphComponent::mapping() const {
    return {FrequencyGrid::plotOf(getLocalBounds().toFloat())};
}

void EqGraphComponent::readBands() {
    for (size_t band = 0; band < bands_.size(); ++band) {
        bands_[band] = params_[band].read();
        enabled_[band] = bands_[band].enabled;
    }
}

// The engine's live gains describe the audio only while blocks are arriving. When the host stops calling
// processBlock they are the last ones it set, and an edit made since would not be in them.
void EqGraphComponent::followAudio(double dtSeconds) {
    audioRunning_ = audioIdle_.update(processor_.audioBlocksProcessed(), dtSeconds);
}

bool EqGraphComponent::updateLiveBands() {
    bool handleLookChanged = false;
    anyDynamic_ = false;
    for (size_t band = 0; band < bands_.size(); ++band) {
        const auto& s = bands_[band];
        const bool live = audioRunning_ && dsp::sounds(s);  // a band that is switched off does nothing to move
        const auto next = withLiveGain(s, live ? processor_.liveGainDb(eq_, static_cast<int>(band)) : s.gainDb);
        liveBands_[band] = holdSmallLiveMoves(liveBands_[band], next);

        // A dynamic band's handle carries its range and live position, and one that is switched off still carries its range track, so
        // what is drawn follows the settings, whether or not the band sounds. Only a band that sounds moves the dashes.
        const bool dynamic = s.enabled && s.dynamic && dsp::hasGain(s.shape);
        anyDynamic_ = anyDynamic_ || (dynamic && dsp::sounds(s));
        if (dynamic != drawnDynamic_[band] || (dynamic && s.rangeDb != drawnRange_[band])) {
            drawnDynamic_[band] = dynamic;
            drawnRange_[band] = s.rangeDb;
            handleLookChanged = true;
        }
    }
    return handleLookChanged;
}

bool EqGraphComponent::followParameters() {
    bool redraw = false;

    readBands();
    const double rate = curveSampleRate();
    if (rate != drawnSampleRate_) {
        drawnSampleRate_ = rate;
        gridDirty_ = true;
        redraw = true;
    }

    const bool bypassed = bypassParam_->load() >= 0.5f || eqBypassParam_->load() >= 0.5f;
    const bool mono = processor_.getMainBusNumOutputChannels() == 1;
    const bool dynamicChanged = updateLiveBands();
    const bool liveChanged = curves_.update(liveBands_, rate, mono);
    const bool staticChanged = anyDynamic_ && staticCurves_.update(bands_, rate, mono);
    dashesDirty_ = dashesDirty_ || staticChanged;
    if (liveChanged || staticChanged || dynamicChanged || bypassed != bypassed_) {
        bypassed_ = bypassed;
        curvesDirty_ = true;
        handlesDirty_ = true;
        redraw = true;
    }

    // Automation, the AI or a restored state may have removed the band in use.
    if (dragBand_ >= 0 && !enabled_[idx(dragBand_)]) stopDragging();
    if (hover_ >= 0 && !enabled_[idx(hover_)]) setHover(-1);
    const int selection = nextSelection(selected_, enabled_);
    if (selection != selected_) {
        selectBand(selection);
        redraw = true;
    }

    if (redraw) repaint();
    return redraw;
}

bool EqGraphComponent::tick(double dtSeconds) {
    ++ticks_;
    followAudio(dtSeconds);
    bool redraw = followParameters();
    if (animateHandles(dtSeconds)) redraw = true;

    if (processor_.pageAnalyzer().readDisplay(frame_) && spectrum_.pushFrame(frame_)) {
        spectrumDirty_ = true;
        redraw = true;
    }
    if (spectrum_.advance(dtSeconds)) {
        spectrumDirty_ = true;
        redraw = true;
    }

    if (noticeSeconds_ > 0.0) {
        noticeSeconds_ -= dtSeconds;
        if (noticeSeconds_ <= kNoticeFadeSeconds) redraw = true;
    }

    if (redraw) repaint();
    return redraw;
}

// Moves every handle's look a step towards its state, so that nothing changes abruptly. Costs nothing once
// they have all arrived.
bool EqGraphComponent::animateHandles(double dtSeconds) {
    const auto step = static_cast<float>(1.0 - std::exp(-dtSeconds / kMotionSeconds));
    const auto approach = [step](float& value, float target) {
        if (value == target) return false;
        value += (target - value) * step;
        if (std::abs(target - value) < kMotionSettled) value = target;
        return true;
    };

    bool moved = false;
    bool selectionMoved = false;
    for (int band = 0; band < dsp::kMaxBands; ++band) {
        auto& motion = motion_[idx(band)];
        moved = approach(motion.presence, enabled_[idx(band)] ? 1.0f : 0.0f) || moved;
        moved = approach(motion.hover, band == hover_ || band == dragBand_ ? 1.0f : 0.0f) || moved;
        selectionMoved = approach(motion.selected, band == selected_ ? 1.0f : 0.0f) || selectionMoved;
    }
    if (moved || selectionMoved) handlesDirty_ = true;
    if (selectionMoved) curvesDirty_ = true;  // The selected band's curve comes forward as its handle does.

    const int tagged = dragBand_ >= 0 ? dragBand_ : (hover_ >= 0 ? hover_ : externalTag_);
    if (tagged >= 0) tagBand_ = tagged;
    const bool tagMoved = approach(tagAlpha_, tagged >= 0 ? 1.0f : 0.0f);

    return moved || selectionMoved || tagMoved;
}

void EqGraphComponent::showValueTag(int band) {
    if (band == externalTag_) return;
    externalTag_ = band;
    repaint();
}

void EqGraphComponent::selectBand(int band) {
    if (band == selected_) return;
    selected_ = band;
    repaint();
}

int EqGraphComponent::bandAt(juce::Point<float> position) const {
    const auto map = mapping();
    std::array<juce::Point<float>, dsp::kMaxBands> handles;
    for (size_t band = 0; band < handles.size(); ++band) handles[band] = handlePosition(map, bands_[band]);
    return hitTestBand(handles, enabled_, position, selected_);
}

void EqGraphComponent::setHover(int band) {
    if (band == hover_) return;
    hover_ = band;
    setMouseCursor(band >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void EqGraphComponent::advanceWheelGesture(double dtSeconds) {
    if (!wheelGestureOpen_) return;
    wheelIdleSeconds_ -= dtSeconds;
    if (wheelIdleSeconds_ <= 0.0) endWheelGesture();
}

void EqGraphComponent::endWheelGesture() {
    if (!wheelGestureOpen_) return;
    wheelGestureOpen_ = false;
    gesture_.end();
}

void EqGraphComponent::stopDragging() {
    dragBand_ = -1;
    gesture_.end();
}

void EqGraphComponent::mouseMove(const juce::MouseEvent& e) {
    setHover(bandAt(e.position));
}

void EqGraphComponent::mouseExit(const juce::MouseEvent&) {
    if (dragBand_ < 0) setHover(-1);
}

void EqGraphComponent::mouseDown(const juce::MouseEvent& e) {
    if (!e.mods.isLeftButtonDown()) return;

    endWheelGesture();
    readBands();

    const int band = bandAt(e.position);
    setHover(band);
    if (band < 0) return;

    selectBand(band);
    dragBand_ = band;
    dragMoved_ = false;
    grabOffset_ = handlePosition(mapping(), bands_[idx(band)]) - e.position;
    gesture_.begin(band);
}

void EqGraphComponent::mouseDrag(const juce::MouseEvent& e) {
    if (dragBand_ < 0) return;
    if (!dragMoved_ && e.getDistanceFromDragStart() < kDragThresholdPixels) return;
    dragMoved_ = true;

    // From the band as it is now, so that a Q or shape changed meanwhile is not written back.
    const auto current = params_[idx(dragBand_)].read();
    gesture_.write(bandAfterDrag(current, mapping(), e.position + grabOffset_));
    followParameters();
}

void EqGraphComponent::mouseUp(const juce::MouseEvent&) { stopDragging(); }

void EqGraphComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    if (!e.mods.isLeftButtonDown()) return;

    readBands();
    const int band = bandAt(e.position);

    if (band >= 0) {
        auto s = bands_[idx(band)];
        s.enabled = false;
        writeBand(processor_.apvts(), eq_, band, s);
        setHover(-1);
    } else {
        const int free = firstFreeBand(enabled_);
        if (free < 0) {
            noticeSeconds_ = kNoticeSeconds;
            repaint();
            return;
        }
        // A bell where the pointer is.
        writeBand(processor_.apvts(), eq_, free, newBandAt(mapping(), e.position));
        selected_ = free;
    }
    followParameters();
}

void EqGraphComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    readBands();
    const int under = bandAt(e.position);
    const int band = dragBand_ >= 0 ? dragBand_ : (under >= 0 ? under : selected_);
    if (band < 0) return;

    auto s = params_[idx(band)].read();
    const float ticks = wheelTicks(wheel);
    if (!s.enabled || !controlsFor(s).q || ticks == 0.0f) return;

    selectBand(band);
    // A drag's gesture takes in the wheel. Otherwise a run of notches is held together as one edit.
    if (dragBand_ < 0) {
        if (!gesture_.active() || gesture_.band() != band) {
            gesture_.begin(band);
            wheelGestureOpen_ = true;
        }
        wheelIdleSeconds_ = kWheelGestureSeconds;
    }
    s.q = applyWheelToQ(s.q, ticks);
    gesture_.write(s);
    followParameters();
}

void EqGraphComponent::resized() {
    gridDirty_ = true;
    labelsDirty_ = true;
    curvesDirty_ = true;
    dashesDirty_ = true;
    handlesDirty_ = true;
    spectrumDirty_ = true;
}

// ---- drawing -------------------------------------------------------------------------------------

bool EqGraphComponent::anyLayerLost() const {
    return layerWasLost(gridLayer_) || layerWasLost(labelLayer_) || layerWasLost(curveLayer_) || layerWasLost(handleLayer_);
}

void EqGraphComponent::clearSpectrum() {
    processor_.pageAnalyzer().readDisplay(frame_);
    spectrum_ = {};
    spectrumDirty_ = true;
    repaint();
}

void EqGraphComponent::swapLayersForTest(std::array<juce::Image, 4>& layers) {
    std::swap(gridLayer_, layers[0]);
    std::swap(labelLayer_, layers[1]);
    std::swap(curveLayer_, layers[2]);
    std::swap(handleLayer_, layers[3]);
}

void EqGraphComponent::redrawLayer(juce::Image& layer, juce::Image::PixelFormat format, float scale, Layer which, const juce::Graphics& painter) {
    ++layerDraws_;
    const int width = std::max(1, juce::roundToInt(static_cast<float>(getWidth()) * scale));
    const int height = std::max(1, juce::roundToInt(static_cast<float>(getHeight()) * scale));
    if (layer.getWidth() != width || layer.getHeight() != height)
        layer = makeLayerImage(format, width, height, *painter.getInternalContext().getPreferredImageTypeForTemporaryImages());

    juce::Graphics g(layer);
    g.getInternalContext().setFill(juce::Colours::transparentBlack);
    g.getInternalContext().fillRect(layer.getBounds(), true);
    g.addTransform(juce::AffineTransform::scale(static_cast<float>(width) / static_cast<float>(getWidth()),
                                                static_cast<float>(height) / static_cast<float>(getHeight())));
    switch (which) {
        case Layer::Grid: paintGrid(g); break;
        case Layer::Labels: paintLabels(g); break;
        case Layer::Curves: paintCurves(g); break;
        case Layer::Handles: paintHandles(g); break;
    }
}

void EqGraphComponent::paintGrid(juce::Graphics& g) const {
    const auto map = mapping();
    const auto whole = getLocalBounds().toFloat();

    FrequencyGrid::paintBackground(g, whole, curveSampleRate(), theme::backgroundTop);

    // Ten dB of the filter scale and 20 dB of the analyzer scale to each line.
    for (int line = -3; line <= 3; ++line) {
        g.setColour(line == 0 ? theme::gridZero : theme::gridMajor);
        g.fillRect(0.0f, map.dbToY(line * 10.0) - 0.5f, whole.getWidth(), 1.0f);
    }
}

// Sits over the analyzer, so that the numbers stay clear of it.
void EqGraphComponent::paintLabels(juce::Graphics& g) const {
    const auto map = mapping();
    const auto whole = getLocalBounds().toFloat();
    constexpr float kInset = FrequencyGrid::kLabelInset, kRowHeight = FrequencyGrid::kLabelRowHeight;

    FrequencyGrid::paintLabels(g, whole);

    // A faint plate behind each number, so that it stays readable when the analyzer runs behind it.
    const auto font = FrequencyGrid::labelFont();
    const auto label = [&](const juce::String& text, juce::Rectangle<float> box, juce::Justification justification, juce::Colour colour) {
        FrequencyGrid::paintLabel(g, font, text, box, justification, colour);
    };

    for (int line = -3; line <= 3; ++line) {
        const int db = line * 10;
        const float y = map.dbToY(db) - kRowHeight - 1.0f;
        const juce::String left = db == 0 ? "0 dB" : (db > 0 ? "+" : "") + juce::String(db);
        label(left, juce::Rectangle<float>(kInset, y, 44.0f, kRowHeight), juce::Justification::centredLeft,
              line == 0 ? theme::textBright : theme::textMid);

        // The analyzer's scale is the quieter of the two, so it names only every other line.
        if ((line + 3) % 2 == 0) {
            const int fs = -20 * (3 - line);
            label(fs == 0 ? "0 dBFS" : juce::String(fs), juce::Rectangle<float>(whole.getRight() - kInset - 52.0f, y, 52.0f, kRowHeight),
                  juce::Justification::centredRight, theme::textMid);
        }
    }

    // What the two analyzer lines are, each named beside a sample of itself.
    const float legendY = map.dbToY(30.0) - kRowHeight - 1.0f;
    const auto legend = [&](float x, const juce::String& text, juce::Colour colour, float thickness) {
        g.setColour(colour);
        g.fillRoundedRectangle(x, legendY + 0.5f * kRowHeight - 0.5f * thickness, 16.0f, thickness, 0.5f * thickness);
        g.setColour(theme::textMid);
        g.drawText(text, juce::Rectangle<float>(x + 21.0f, legendY, 44.0f, kRowHeight), juce::Justification::centredLeft, false);
    };
    legend(whole.getRight() - 196.0f, "Input", theme::spectrumBefore.brighter(0.4f), 1.5f);
    legend(whole.getRight() - 130.0f, "Output", theme::spectrumAfter, 2.0f);
    if (anyDynamic_) {
        // The curve at the gains the controls set, drawn dashed beneath the live one.
        const float x = whole.getRight() - 262.0f;
        g.setColour(theme::totalCurve.withAlpha(0.6f));
        for (int dash = 0; dash < 3; ++dash)
            g.fillRect(x + 6.0f * static_cast<float>(dash), legendY + 0.5f * kRowHeight - 0.5f, 4.0f, 1.0f);
        g.setColour(theme::textMid);
        g.drawText("Static", juce::Rectangle<float>(x + 21.0f, legendY, 44.0f, kRowHeight), juce::Justification::centredLeft, false);
    }

    if (bypassed_) {
        const juce::String text = "Bypassed: the audio passes through unchanged";
        const auto noticeFont = theme::font(theme::kSizeBody, true);
        const float width = juce::GlyphArrangement::getStringWidth(noticeFont, text) + 28.0f;
        const auto box = juce::Rectangle<float>(width, 28.0f).withCentre({whole.getCentreX(), 40.0f});
        g.setColour(theme::caution);
        g.fillRoundedRectangle(box, 14.0f);
        g.setColour(theme::backgroundTop);
        g.setFont(noticeFont);
        g.drawText(text, box, juce::Justification::centred, false);
    }
}

void EqGraphComponent::paintCurves(juce::Graphics& g) const {
    const auto map = mapping();
    const float zeroY = map.dbToY(0.0);
    const float dim = bypassed_ ? kBypassedOpacity : 1.0f;

    // The quiet bands first, so that the selected one ends up on top of them.
    const auto paintBand = [&](int band) {
        const auto& curve = curves_.band(band);
        if (curve.empty()) return;
        const float selected = motion_[idx(band)].selected;
        const auto colour = theme::bandColour(band);
        const float own = (bands_[idx(band)].bypassed ? kOffCurveOpacity : 1.0f) * dim;

        juce::Path fill;
        addCurveTo(fill, curve, map);
        fill.lineTo(map.freqToX(curve.back().hz), zeroY);
        fill.lineTo(map.freqToX(curve.front().hz), zeroY);
        fill.closeSubPath();
        g.setGradientFill(fadingFromZero(map, colour, (0.15f + 0.13f * selected) * own));
        g.fillPath(fill);

        juce::Path line;
        addActivePartsTo(line, curve, map);
        g.setColour(colour.withAlpha((0.55f + 0.40f * selected) * own));
        g.strokePath(line, juce::PathStrokeType(1.2f + 0.4f * selected));
    };
    for (int band = 0; band < dsp::kMaxBands; ++band)
        if (band != selected_) paintBand(band);
    if (selected_ >= 0) paintBand(selected_);

    const auto stroke = [](float thickness) {
        return juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    };
    const auto paintTotal = [&](dsp::ChannelMode mode, float strength) {
        const auto& total = curves_.curve(mode);
        juce::Path line;
        addCurveTo(line, total, map);

        juce::Path fill(line);
        fill.lineTo(map.freqToX(total.back().hz), zeroY);
        fill.lineTo(map.freqToX(total.front().hz), zeroY);
        fill.closeSubPath();
        g.setGradientFill(fadingFromZero(map, theme::totalGlow, 0.09f * strength * dim));
        g.fillPath(fill);

        g.setColour(theme::totalGlow.withAlpha(0.06f * strength * dim));
        g.strokePath(line, stroke(9.0f));
        g.setColour(theme::totalGlow.withAlpha(0.14f * strength * dim));
        g.strokePath(line, stroke(4.5f));
        g.setColour(theme::totalCurve.withAlpha(strength * dim));
        g.strokePath(line, stroke(1.2f + 0.5f * strength));
    };

    // With separate paths on show, the selected band's own comes forward. A stereo band is in all of them.
    constexpr std::array<dsp::ChannelMode, 5> modes{dsp::ChannelMode::Stereo, dsp::ChannelMode::Left, dsp::ChannelMode::Right,
                                                    dsp::ChannelMode::Mid, dsp::ChannelMode::Side};

    // While any band is dynamic, the curve at the set gains stays under the live one, faint and dashed.
    if (anyDynamic_) {
        g.setColour(theme::totalCurve.withAlpha(0.34f * dim));
        for (const auto& dashes : staticDashes_) g.fillPath(dashes);
    }

    const bool split = !curves_.isCurveShown(dsp::ChannelMode::Stereo);
    const auto emphasised = split && selected_ >= 0 ? bands_[idx(selected_)].channel : dsp::ChannelMode::Stereo;
    const bool anyEmphasised = emphasised != dsp::ChannelMode::Stereo;
    for (const auto mode : modes)
        if (curves_.isCurveShown(mode) && !(anyEmphasised && mode == emphasised)) paintTotal(mode, anyEmphasised ? 0.5f : 1.0f);
    if (anyEmphasised) paintTotal(emphasised, 1.0f);

    if (split) {
        // Names each curve where it ends, moving a name aside when two ends meet.
        g.setFont(theme::font(theme::kSizeSmall, true));
        std::vector<float> taken;
        for (const auto mode : modes) {
            if (!curves_.isCurveShown(mode)) continue;
            const auto& total = curves_.curve(mode);
            const auto db = juce::jlimit(-kGraphRangeDb, kGraphRangeDb, static_cast<double>(total.back().db));
            float y = map.dbToY(db);
            while (std::any_of(taken.begin(), taken.end(), [y](float other) { return std::abs(other - y) < 12.0f; })) y -= 12.0f;
            taken.push_back(y);
            g.setColour(theme::totalCurve.withAlpha((mode == emphasised || !anyEmphasised ? 1.0f : 0.6f) * dim));
            g.drawText(channelLetter(mode), juce::Rectangle<float>(map.freqToX(total.back().hz) - 16.0f, y - 14.0f, 14.0f, 13.0f),
                       juce::Justification::centredRight, false);
        }
    }

    bool any = false;
    for (const auto enabled : enabled_) any = any || enabled;
    if (!any) {
        g.setColour(theme::textMid);
        g.setFont(theme::font(theme::kSizeHint));
        g.drawText("Double-click anywhere to add a band", getLocalBounds().toFloat().withBottom(zeroY - 10.0f),
                   juce::Justification::centredBottom, false);
    }
}

// Slow to make, and the set gains seldom move while the live ones move all the time.
void EqGraphComponent::rebuildStaticDashes() {
    dashesDirty_ = false;
    ++dashBuilds_;
    for (auto& path : staticDashes_) path.clear();

    const auto map = mapping();
    constexpr std::array<float, 2> dashes{5.0f, 4.0f};
    size_t next = 0;
    for (const auto mode : {dsp::ChannelMode::Stereo, dsp::ChannelMode::Left, dsp::ChannelMode::Right, dsp::ChannelMode::Mid, dsp::ChannelMode::Side}) {
        if (!staticCurves_.isCurveShown(mode)) continue;
        juce::Path line;
        addCurveTo(line, staticCurves_.curve(mode), map);
        juce::PathStrokeType(1.1f).createDashedStroke(staticDashes_[next++], line, dashes.data(), static_cast<int>(dashes.size()));
    }
}

void EqGraphComponent::paintHandle(juce::Graphics& g, int band) const {
    const auto& motion = motion_[idx(band)];
    if (motion.presence <= 0.0f) return;

    const auto& s = bands_[idx(band)];
    const auto map = mapping();
    const auto at = handlePosition(map, s);
    const auto colour = theme::bandColour(band);
    const float alpha = motion.presence * (bypassed_ ? kBypassedOpacity : 1.0f);
    // A band that is switched off is a dim ring with its number: hollow, and nothing of its dynamics moving.
    const bool off = s.bypassed;
    const float ring = off ? kOffHandleOpacity : 1.0f;
    const float active = std::max(motion.hover, motion.selected);
    const float radius = kHandleRadius * (0.8f + 0.2f * motion.presence) + motion.hover * 1.0f;

    // A thin stem down to the 0 dB line shows how far the selected band has moved from it.
    if (motion.selected > 0.0f && dsp::hasGain(s.shape)) {
        const float zero = map.dbToY(0.0);
        g.setColour(colour.withAlpha(0.45f * motion.selected * alpha * ring));
        // A hollow ring has nothing to hide the stem, so it stops at the ring's edge instead of running through the number.
        const float from = off ? (at.y < zero ? at.y + radius : at.y - radius) : at.y;
        g.fillRect(at.x - 0.5f, std::min(zero, from), 1.0f, std::max(0.0f, off && std::abs(zero - at.y) < radius ? 0.0f : std::abs(zero - from)));
    }

    // A dynamic band's handle stays at its set gain. Along its vertical, a track runs to where its range would
    // take it, and a bead rides on it at the gain the band is using now.
    if (s.dynamic && dsp::hasGain(s.shape)) {
        const auto limited = [](double db) { return juce::jlimit(-kGraphRangeDb, kGraphRangeDb, db); };
        const float endY = map.dbToY(limited(s.gainDb + s.rangeDb));
        const float liveY = map.dbToY(limited(liveBands_[idx(band)].gainDb));

        g.setColour(colour.withAlpha((0.16f + 0.16f * motion.selected) * alpha * ring));
        g.fillRoundedRectangle(at.x - 2.5f, std::min(at.y, endY), 5.0f, std::abs(endY - at.y), 2.5f);
        g.setColour(colour.withAlpha((0.55f + 0.30f * motion.selected) * alpha * ring));
        g.fillRoundedRectangle(at.x - 5.0f, endY - 1.0f, 10.0f, 2.0f, 1.0f);

        if (!off) {
            const juce::Point<float> bead{at.x, liveY};
            g.setColour(juce::Colours::black.withAlpha(0.45f * alpha));
            g.fillEllipse(juce::Rectangle<float>(10.0f, 10.0f).withCentre(bead.translated(0.0f, 1.0f)));
            g.setColour(theme::textBright.withMultipliedAlpha(alpha));
            g.fillEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre(bead));
            g.setColour(colour.withMultipliedAlpha(alpha));
            g.fillEllipse(juce::Rectangle<float>(4.5f, 4.5f).withCentre(bead));
        }
    }

    // Round for shapes whose gain the handle carries, squared off for those it doesn't. A band on one
    // channel widens into a pill that holds the channel's letter beside its number.
    const auto letter = channelLetter(s.channel);
    const float pillExtra = letter.isNotEmpty() ? 10.0f : 0.0f;
    const float cornerRatio = dsp::hasGain(s.shape) ? 1.0f : 0.42f;
    const auto bead = [&](float r) {
        return std::pair{juce::Rectangle<float>(2.0f * r + pillExtra, 2.0f * r).withCentre(at), r * cornerRatio};
    };

    if (!off) {
        const auto [shadow, shadowCorner] = bead(radius + 2.0f);
        g.setColour(juce::Colours::black.withAlpha(0.4f * alpha));
        g.fillRoundedRectangle(shadow.translated(0.0f, 1.0f), shadowCorner);
    }

    if (active > 0.0f) {
        const auto [halo, haloCorner] = bead(radius + 2.0f + 4.0f * active);
        g.setColour(colour.withAlpha((0.10f * motion.hover + 0.16f * motion.selected) * alpha * ring));
        g.fillRoundedRectangle(halo, haloCorner);
    }

    // Hollow when switched off: the curves and the grid show through, and the ring is thin and faint.
    const auto [body, bodyCorner] = bead(radius);
    if (!off) {
        g.setColour(theme::backgroundTop.interpolatedWith(colour, motion.selected).withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(body, bodyCorner);
    }
    g.setColour(colour.interpolatedWith(juce::Colours::white, 0.35f * motion.hover).withMultipliedAlpha(alpha * ring));
    g.drawRoundedRectangle(body.reduced(0.75f), bodyCorner, off ? 1.0f : 1.5f);

    // The number stays readable: on a hollow ring it is light, whether or not the band is selected.
    const auto ink = off ? theme::textBright.withMultipliedAlpha(alpha * 0.8f)
                         : theme::textBright.interpolatedWith(theme::backgroundTop, motion.selected).withMultipliedAlpha(alpha);
    g.setColour(ink);
    g.setFont(theme::font(theme::kSizeSmall, true));
    if (letter.isEmpty()) {
        g.drawText(juce::String(band + 1), body.expanded(3.0f, 0.0f), juce::Justification::centred, false);
        return;
    }

    auto numberArea = body;
    const auto letterArea = numberArea.removeFromRight(0.42f * body.getWidth());
    g.drawText(juce::String(band + 1), numberArea.withTrimmedLeft(2.0f), juce::Justification::centred, false);
    g.setColour(off ? colour.withMultipliedAlpha(alpha * 0.8f) : colour.interpolatedWith(theme::backgroundTop, motion.selected).withMultipliedAlpha(alpha));
    g.drawText(letter, letterArea.withTrimmedRight(2.0f), juce::Justification::centred, false);
}

void EqGraphComponent::paintHandles(juce::Graphics& g) const {
    for (int band = 0; band < dsp::kMaxBands; ++band)
        if (band != selected_) paintHandle(g, band);
    if (selected_ >= 0) paintHandle(g, selected_);
}

void EqGraphComponent::paintValueTag(juce::Graphics& g, int band) const {
    const auto& s = bands_[idx(band)];
    const auto controls = controlsFor(s);

    juce::String text = formatFrequency(s.frequencyHz);
    if (controls.gain) text << "    " << formatGain(s.gainDb);
    if (controls.q) text << "    Q " << formatQ(s.q);
    if (controls.slope) text << "    " << effectiveSlope(s.shape, s.slopeDbPerOct) << " dB/oct";

    const auto font = theme::font(theme::kSizeBody);
    const float width = juce::GlyphArrangement::getStringWidth(font, text) + 36.0f;
    const auto whole = getLocalBounds().toFloat().reduced(6.0f);
    const auto zeroY = mapping().dbToY(0.0);
    const auto at = handlePosition(mapping(), s);

    // On the side away from the 0 dB line, so that it doesn't hide the curve it describes.
    const float offset = kHandleRadius + 26.0f;
    const bool below = at.y > zeroY + 1.0f || at.y - offset - 13.0f < whole.getY();
    const auto tag = juce::Rectangle<float>(width, 26.0f).withCentre({at.x, below ? at.y + offset : at.y - offset}).constrainedWithin(whole);

    g.setColour(juce::Colours::black.withAlpha(0.35f * tagAlpha_));
    g.fillRoundedRectangle(tag.expanded(2.0f).translated(0.0f, 2.0f), 8.0f);
    g.setColour(theme::popup.withAlpha(0.97f * tagAlpha_));
    g.fillRoundedRectangle(tag, 7.0f);
    g.setColour(theme::hairline.withMultipliedAlpha(tagAlpha_));
    g.drawRoundedRectangle(tag.reduced(0.5f), 7.0f, 1.0f);

    g.setColour(theme::bandColour(band).withMultipliedAlpha(tagAlpha_));
    g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({tag.getX() + 14.0f, tag.getCentreY()}));

    g.setColour(theme::textBright.withMultipliedAlpha(tagAlpha_));
    g.setFont(font);
    g.drawText(text, tag.withTrimmedLeft(24.0f), juce::Justification::centredLeft, false);
}

void EqGraphComponent::paintNotice(juce::Graphics& g) const {
    const auto alpha = static_cast<float>(juce::jlimit(0.0, 1.0, noticeSeconds_ / kNoticeFadeSeconds));
    const juce::String text = "All 24 bands are in use. Remove one to add another.";
    const auto font = theme::font(theme::kSizeBody);
    const float width = juce::GlyphArrangement::getStringWidth(font, text) + 32.0f;
    const auto box = juce::Rectangle<float>(width, 32.0f).withCentre({static_cast<float>(getWidth()) * 0.5f, 42.0f});

    g.setColour(juce::Colours::black.withAlpha(0.35f * alpha));
    g.fillRoundedRectangle(box.expanded(2.0f).translated(0.0f, 2.0f), 9.0f);
    g.setColour(theme::popup.withAlpha(0.97f * alpha));
    g.fillRoundedRectangle(box, 8.0f);
    g.setColour(theme::totalCurve.withAlpha(0.45f * alpha));
    g.drawRoundedRectangle(box.reduced(0.5f), 8.0f, 1.0f);
    g.setColour(theme::textBright.withAlpha(alpha));
    g.setFont(font);
    g.drawText(text, box, juce::Justification::centred, false);
}

void EqGraphComponent::rebuildSpectrumPaths() {
    spectrumDirty_ = false;
    for (auto* path : {&beforeLine_, &afterLine_, &afterFill_}) path->clear();
    if (!spectrum_.hasData() || spectrum_.validPoints() < 2) return;

    const auto map = mapping();
    const auto bottom = static_cast<float>(getHeight());
    const auto build = [&](const std::vector<float>& shown, juce::Path& line) {
        const int count = spectrum_.validPoints();
        const auto pointAt = [&](int i) {
            return juce::Point<float>(map.freqToX(SpectrumDisplay::pointHz(i)), map.spectrumDbToY(static_cast<double>(shown[idx(i)])));
        };
        // Through the midpoints between readings, so that the line has no corners at any window size.
        line.startNewSubPath(pointAt(0));
        for (int i = 1; i < count - 1; ++i) {
            const auto here = pointAt(i);
            const auto next = pointAt(i + 1);
            line.quadraticTo(here, {0.5f * (here.x + next.x), 0.5f * (here.y + next.y)});
        }
        line.lineTo(pointAt(count - 1));

    };
    build(spectrum_.before(), beforeLine_);
    build(spectrum_.after(), afterLine_);

    afterFill_ = afterLine_;
    const int last = spectrum_.validPoints() - 1;
    afterFill_.lineTo(map.freqToX(SpectrumDisplay::pointHz(last)), bottom);
    afterFill_.lineTo(map.freqToX(SpectrumDisplay::pointHz(0)), bottom);
    afterFill_.closeSubPath();
}

void EqGraphComponent::paint(juce::Graphics& g) {
    if (getWidth() <= 0 || getHeight() <= 0) return;

    // Each layer is drawn from vectors at the screen's own pixel scale, and again whenever that changes.
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (scale != layerScale_) {
        layerScale_ = scale;
        gridDirty_ = true;
        labelsDirty_ = true;
        curvesDirty_ = true;
        handlesDirty_ = true;
    }
    gridDirty_ = gridDirty_ || layerWasLost(gridLayer_);
    labelsDirty_ = labelsDirty_ || layerWasLost(labelLayer_) || anyDynamic_ != labelsDynamic_ || bypassed_ != labelsBypassed_;
    curvesDirty_ = curvesDirty_ || layerWasLost(curveLayer_);
    handlesDirty_ = handlesDirty_ || layerWasLost(handleLayer_);
    if (gridDirty_) {
        redrawLayer(gridLayer_, juce::Image::RGB, scale, Layer::Grid, g);
        gridDirty_ = false;
    }
    if (labelsDirty_) {
        redrawLayer(labelLayer_, juce::Image::ARGB, scale, Layer::Labels, g);
        ++labelDraws_;
        labelsDirty_ = false;
        labelsDynamic_ = anyDynamic_;
        labelsBypassed_ = bypassed_;
    }
    if (curvesDirty_) {
        if (dashesDirty_ && anyDynamic_) rebuildStaticDashes();
        redrawLayer(curveLayer_, juce::Image::ARGB, scale, Layer::Curves, g);
        curvesDirty_ = false;
    }
    if (handlesDirty_) {
        redrawLayer(handleLayer_, juce::Image::ARGB, scale, Layer::Handles, g);
        handlesDirty_ = false;
    }
    if (spectrumDirty_) rebuildSpectrumPaths();

    const auto whole = getLocalBounds().toFloat();
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.setOpacity(1.0f);
    g.drawImage(gridLayer_, whole);

    if (!beforeLine_.isEmpty()) {
        const auto map = mapping();
        g.setColour(theme::spectrumBefore.withAlpha(0.55f));
        g.strokePath(beforeLine_, juce::PathStrokeType(1.0f));

        g.setGradientFill(juce::ColourGradient(theme::spectrumAfter.withAlpha(0.16f), 0.0f, map.spectrumDbToY(kSpectrumTopDb),
                                               theme::spectrumAfter.withAlpha(0.0f), 0.0f, map.spectrumDbToY(FrequencyGrid::kSpectrumFadeDb), false));
        g.fillPath(afterFill_);
        g.setColour(theme::spectrumAfter.withAlpha(0.85f));
        g.strokePath(afterLine_, juce::PathStrokeType(1.1f));
    }

    g.setOpacity(1.0f);
    g.drawImage(labelLayer_, whole);
    g.drawImage(curveLayer_, whole);
    g.drawImage(handleLayer_, whole);

    if (tagAlpha_ > 0.0f && tagBand_ >= 0 && motion_[idx(tagBand_)].presence > 0.0f) paintValueTag(g, tagBand_);
    if (noticeSeconds_ > 0.0) paintNotice(g);
}

}  // namespace smartq
