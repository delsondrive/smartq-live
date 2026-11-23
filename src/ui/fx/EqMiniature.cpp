#include "ui/fx/EqMiniature.h"

#include <algorithm>
#include <cmath>

#include "ui/fx/ChainHost.h"
#include "ui/fx/PageLook.h"
#include "ui/fx/Theme.h"

namespace smartq {

namespace {

constexpr double kRangeDb = 14.0;  // the miniature's own scale, finer than the graph's, so that a few dB can be read
constexpr float kInsetX = 3.0f, kInsetY = 4.0f;
constexpr float kDotRadius = 2.9f;
constexpr double kMiniSpectrumTopDb = -20.0, kMiniSpectrumFloorDb = -100.0;
constexpr float kDimmed = 0.4f;
constexpr float kOffDot = 0.3f;  // the dot of a band that is switched off
constexpr float kCorner = 4.0f;

size_t idx(int n) { return static_cast<size_t>(n); }

// A curve with something to show: a low cut, a dip in the low mids, a lift in the presence and a little air.
BandArray sampleBands() {
    BandArray bands{};
    const auto set = [&](int band, dsp::Shape shape, double hz, double gainDb, double q) {
        auto& s = bands[idx(band)];
        s.enabled = true;
        s.shape = shape;
        s.frequencyHz = hz;
        s.gainDb = gainDb;
        s.q = q;
    };
    set(0, dsp::Shape::LowCut, 42.0, 0.0, 0.7071);
    set(1, dsp::Shape::Bell, 320.0, -4.5, 1.4);
    set(2, dsp::Shape::Bell, 3200.0, 8.0, 0.9);
    set(3, dsp::Shape::HighShelf, 9500.0, 4.0, 0.8);
    return bands;
}

// The shape of music's spectrum: falling with frequency, with weight in the low mids and some bite in the presence.
double sampleSpectrumDb(double hz) {
    const auto octaves = [hz](double centre) { return std::log2(hz / centre); };
    double db = -36.0 - 4.4 * std::log2(hz / 100.0);
    db += 8.0 * std::exp(-octaves(120.0) * octaves(120.0) / 0.9);
    db += 4.0 * std::exp(-octaves(2800.0) * octaves(2800.0) / 1.4);
    db -= 14.0 * std::max(0.0, octaves(11000.0));
    db -= 20.0 * std::max(0.0, -octaves(50.0));
    return db;
}

}  // namespace

EqMiniature::EqMiniature(ChainHost& processor, int eq, MiniatureData data) : processor_(&processor), sample_(data == MiniatureData::Sample) {
    if (!sample_) {
        for (int band = 0; band < dsp::kMaxBands; ++band) params_[idx(band)] = lookupBandParams(processor.apvts(), eq, band);
        for (size_t band = 0; band < bands_.size(); ++band) bands_[band] = params_[band].read();

        // A slot that is not in the chain and holds no band shows the sample, for there is nothing of its own to show.
        if (data == MiniatureData::Automatic) {
            const bool empty = std::none_of(bands_.begin(), bands_.end(), [](const auto& s) { return s.enabled; });
            const auto chain = processor.chain();
            if (empty && std::find(chain.begin(), chain.end(), eq) == chain.end()) sample_ = true;
        }
    }

    if (sample_) bands_ = sampleBands();
    follow();  // The first look is not a change.
}

bool EqMiniature::follow() {
    if (!sample_)
        for (size_t band = 0; band < bands_.size(); ++band) bands_[band] = params_[band].read();

    const double rate = sample_ || processor_->getSampleRate() <= 0.0 ? kFallbackSampleRate : processor_->getSampleRate();
    const bool mono = !sample_ && processor_->getMainBusNumOutputChannels() == 1;
    bool moved = curves_.update(bands_, rate, mono);

    for (size_t band = 0; band < bands_.size(); ++band) {
        const auto& s = bands_[band];
        const Dot dot = s.enabled ? Dot{true, s.bypassed, s.frequencyHz, dsp::hasGain(s.shape) ? s.gainDb : 0.0} : Dot{};
        if (dot == dots_[band]) continue;
        dots_[band] = dot;
        moved = true;
    }
    if (moved) overPicture_.invalidate();
    return moved;
}

bool EqMiniature::update(double) {
    if (sample_) return false;  // it is a picture

    bool moved = follow();
    if (feed_ != seenFeed_ || (feed_ != nullptr && feed_->version != seenSpectrum_)) {
        seenFeed_ = feed_;
        seenSpectrum_ = feed_ != nullptr ? feed_->version : 0;
        moved = true;
    }
    if (moved) repaint();
    return moved;
}

void EqMiniature::paint(juce::Graphics& g) {
    const auto whole = getLocalBounds().toFloat();
    if (whole.isEmpty()) return;

    const float dim = isDimmed() ? kDimmed : 1.0f;
    const GraphMapping map{whole.reduced(kInsetX, kInsetY)};

    juce::Path clip;
    clip.addRoundedRectangle(whole, kCorner);
    g.saveState();
    g.reduceClipRegion(clip);

    // The spectrum under everything, in ember: the chain's input, or for a sample a typical one.
    const auto spectrumY = [&](double db) {
        const double proportion = juce::jlimit(0.0, 1.0, (db - kMiniSpectrumFloorDb) / (kMiniSpectrumTopDb - kMiniSpectrumFloorDb));
        return whole.getBottom() - static_cast<float>(proportion) * 0.92f * whole.getHeight();
    };
    juce::Path spectrum;
    float firstX = 0.0f, lastX = 0.0f;
    const auto addSpectrum = [&](int count, const auto& hzAt, const auto& dbAt) {
        if (count < 2) return;
        firstX = map.freqToX(hzAt(0));
        lastX = map.freqToX(hzAt(count - 1));
        spectrum.startNewSubPath(firstX, spectrumY(dbAt(0)));
        for (int i = 1; i < count; ++i) spectrum.lineTo(map.freqToX(hzAt(i)), spectrumY(dbAt(i)));
    };
    if (sample_) {
        constexpr int kSamplePoints = 64;
        addSpectrum(kSamplePoints, [](int i) { return logSpacedHz(i, kSamplePoints); }, [](int i) { return sampleSpectrumDb(logSpacedHz(i, kSamplePoints)); });
    } else if (feed_ != nullptr && feed_->display.hasData()) {
        const auto& levels = feed_->display.before();
        addSpectrum(feed_->display.validPoints(), [](int i) { return SpectrumDisplay::pointHz(i); },
                    [&levels](int i) { return static_cast<double>(levels[idx(i)]); });
    }
    if (!spectrum.isEmpty()) {
        juce::Path fill(spectrum);
        fill.lineTo(lastX, whole.getBottom());
        fill.lineTo(firstX, whole.getBottom());
        fill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(theme::spectrumAfter.withAlpha(0.24f * dim), 0.0f, whole.getY(),
                                               theme::spectrumAfter.withAlpha(0.03f * dim), 0.0f, whole.getBottom(), false));
        g.fillPath(fill);
        g.setColour(theme::spectrumAfter.withAlpha(0.50f * dim));
        g.strokePath(spectrum, juce::PathStrokeType(0.9f));
    }

    if (isDimmed() != overPictureDimmed_) {
        overPictureDimmed_ = isDimmed();
        overPicture_.invalidate();
    }
    overPicture_.draw(g, getLocalBounds(), [this](juce::Graphics& over) { paintOverSpectrum(over); });
    g.restoreState();
}

void EqMiniature::paintOverSpectrum(juce::Graphics& g) const {
    const auto whole = getLocalBounds().toFloat();
    const float dim = isDimmed() ? kDimmed : 1.0f;
    const auto plot = whole.reduced(kInsetX, kInsetY);
    const GraphMapping map{plot};
    const float zeroY = plot.getCentreY();
    const auto dbToY = [&](double db) {
        return zeroY - static_cast<float>(juce::jlimit(-1.0, 1.0, db / kRangeDb)) * 0.5f * plot.getHeight();
    };

    g.setColour(juce::Colours::white.withAlpha(0.16f * dim));
    g.fillRect(whole.getX(), zeroY - 0.5f, whole.getWidth(), 1.0f);

    // The curve, with the same soft glow as on the page.
    constexpr std::array<dsp::ChannelMode, 5> modes{dsp::ChannelMode::Stereo, dsp::ChannelMode::Left, dsp::ChannelMode::Right,
                                                    dsp::ChannelMode::Mid, dsp::ChannelMode::Side};
    for (const auto mode : modes) {
        if (!curves_.isCurveShown(mode)) continue;
        const auto& total = curves_.curve(mode);
        if (total.size() < 2) continue;

        juce::Path line;
        for (size_t i = 0; i < total.size(); ++i) {
            const juce::Point<float> at{map.freqToX(total[i].hz), dbToY(total[i].db)};
            if (i == 0)
                line.startNewSubPath(at);
            else
                line.lineTo(at);
        }

        juce::Path fill(line);
        fill.lineTo(map.freqToX(total.back().hz), zeroY);
        fill.lineTo(map.freqToX(total.front().hz), zeroY);
        fill.closeSubPath();
        g.setColour(theme::totalGlow.withAlpha(0.10f * dim));
        g.fillPath(fill);

        const auto stroke = [](float thickness) {
            return juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        };
        g.setColour(theme::totalGlow.withAlpha(0.16f * dim));
        g.strokePath(line, stroke(4.0f));
        g.setColour(theme::totalCurve.withAlpha(dim));
        g.strokePath(line, stroke(1.5f));
    }

    // A dot for each enabled band, on the curve where it acts, in the colour it has on the page.
    for (int band = 0; band < dsp::kMaxBands; ++band) {
        const auto& dot = dots_[idx(band)];
        if (!dot.on) continue;
        const juce::Point<float> at{map.freqToX(dot.hz), dbToY(dot.db)};
        const float own = dot.off ? kOffDot : 1.0f;
        g.setColour(theme::backgroundTop.withAlpha(0.85f * dim * own));
        g.fillEllipse(juce::Rectangle<float>(2.0f * kDotRadius + 2.0f, 2.0f * kDotRadius + 2.0f).withCentre(at));
        g.setColour(theme::bandColour(band).withAlpha(dim * own));
        g.fillEllipse(juce::Rectangle<float>(2.0f * kDotRadius, 2.0f * kDotRadius).withCentre(at));
    }
}

}  // namespace smartq
