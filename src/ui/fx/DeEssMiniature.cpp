#include "ui/fx/DeEssMiniature.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "ui/fx/ChainHost.h"
#include "ui/fx/GraphMapping.h"
#include "ui/fx/ModuleHistory.h"
#include "ui/fx/PageLook.h"
#include "ui/fx/Theme.h"

namespace smartq {

namespace {

constexpr float kDimmed = 0.4f;
constexpr float kLineThickness = 1.6f;
constexpr double kBandEdgeDb = -6.0;
constexpr double kRedrawStepDb = 0.1;

// The mockup's picture, in a 168 x 44 frame: the line across at 0 dB, dipping to the foot at the most the range can take.
constexpr float kFrameWidth = 168.0f, kFrameHeight = 44.0f;
constexpr float kLineTop = 14.0f, kLineFoot = 42.0f;
constexpr double kLineRangeDb = 24.0;

// The chain input's spectrum on the miniature's own scale, as the EQ's miniature has it.
constexpr double kMiniSpectrumTopDb = -20.0, kMiniSpectrumFloorDb = -100.0;

constexpr std::array<juce::Point<float>, 14> kSampleSpectrum{{{0, 34}, {16, 26}, {32, 30}, {50, 28}, {70, 32}, {90, 30}, {106, 33},
                                                              {120, 18}, {130, 12}, {140, 16}, {150, 30}, {160, 38}, {168, 42}, {168, 44}}};
constexpr float kSampleBandX = 116.0f, kSampleBandWidth = 34.0f;

}  // namespace

juce::Range<double> deEssBandHz(double frequencyHz, double sampleRate) {
    const double middle = dsp::DeEssModule::bandMiddleHz(frequencyHz, sampleRate);
    // Halving the stretch in log frequency where the gain crosses the edge, from the middle out to each end of what is drawn.
    const auto edge = [&](double far) {
        double inside = std::log(middle), outside = std::log(far);
        if (dsp::DeEssModule::bandGainDb(far, frequencyHz, sampleRate) >= kBandEdgeDb) return far;
        for (int i = 0; i < 40; ++i) {
            const double halfway = 0.5 * (inside + outside);
            if (dsp::DeEssModule::bandGainDb(std::exp(halfway), frequencyHz, sampleRate) >= kBandEdgeDb)
                inside = halfway;
            else
                outside = halfway;
        }
        return std::exp(inside);
    };
    return {edge(kGraphMinHz), edge(std::min(kGraphMaxHz, 0.5 * sampleRate))};
}

DeEssMiniature::DeEssMiniature(ChainHost& processor, int slot, MiniatureData data) : processor_(processor) {
    params_ = DeEssParams{lookupControlValues(processor.apvts(), slot)};
    sample_ = data == MiniatureData::Sample;
    if (data == MiniatureData::Automatic) {
        const auto chain = processor.chain();
        sample_ = std::find(chain.begin(), chain.end(), slot) == chain.end();
    }
    settings_ = params_.read();
}

double DeEssMiniature::sampleRate() const { return processor_.getSampleRate() > 0.0 ? processor_.getSampleRate() : kFallbackSampleRate; }

bool DeEssMiniature::update(double) {
    if (sample_) return false;  // it is a picture

    bool moved = false;
    const auto settings = params_.read();
    if (settings.frequencyHz != settings_.frequencyHz || settings.mode != settings_.mode) moved = true;
    settings_ = settings;

    const double reduction = history_ != nullptr ? history_->reductionNowDb() : 0.0;
    if (std::abs(reduction - reductionDb_) >= kRedrawStepDb || ((reduction > 0.0) != (reductionDb_ > 0.0))) {
        reductionDb_ = reduction;
        moved = true;
    }

    if (feed_ != seenFeed_ || (feed_ != nullptr && feed_->version != seenSpectrum_)) {
        seenFeed_ = feed_;
        seenSpectrum_ = feed_ != nullptr ? feed_->version : 0;
        moved = true;
    }
    if (moved) repaint();
    return moved;
}

void DeEssMiniature::paint(juce::Graphics& g) {
    const auto area = getLocalBounds().toFloat();
    if (area.getWidth() < 2.0f) return;
    const float dim = isDimmed() ? kDimmed : 1.0f;
    const auto accent = theme::moduleAccent(ModuleType::DeEss);
    const auto inFrame = [&](juce::Point<float> at) {
        return juce::Point<float>{area.getX() + at.x / kFrameWidth * area.getWidth(), area.getY() + at.y / kFrameHeight * area.getHeight()};
    };
    const auto lineY = [&](double db) {
        const auto share = static_cast<float>(juce::jlimit(0.0, 1.0, -db / kLineRangeDb));
        return inFrame({0.0f, kLineTop + share * (kLineFoot - kLineTop)}).y;
    };

    juce::Rectangle<float> band;
    juce::Path spectrum, line;
    if (sample_) {
        band = {inFrame({kSampleBandX, 0.0f}).x, area.getY(), kSampleBandWidth / kFrameWidth * area.getWidth(), area.getHeight()};
        spectrum.startNewSubPath(inFrame({0.0f, kFrameHeight}));
        for (const auto& at : kSampleSpectrum) spectrum.lineTo(inFrame(at));
        spectrum.closeSubPath();
        line.startNewSubPath(inFrame({0.0f, 14.0f}));
        line.lineTo(inFrame({110.0f, 14.0f}));
        line.cubicTo(inFrame({120.0f, 14.0f}), inFrame({124.0f, 29.0f}), inFrame({133.0f, 29.0f}));
        line.cubicTo(inFrame({142.0f, 29.0f}), inFrame({146.0f, 14.0f}), inFrame({156.0f, 14.0f}));
        line.lineTo(inFrame({168.0f, 14.0f}));
    } else {
        const GraphMapping map{area};
        const double rate = sampleRate();
        const auto stretch = deEssBandHz(settings_.frequencyHz, rate);
        band = juce::Rectangle<float>::leftTopRightBottom(map.freqToX(stretch.getStart()), area.getY(), map.freqToX(stretch.getEnd()), area.getBottom());

        if (feed_ != nullptr && feed_->display.hasData() && feed_->display.validPoints() >= 2) {
            const auto& levels = feed_->display.before();
            const int count = feed_->display.validPoints();
            const auto spectrumY = [&](double db) {
                const double share = juce::jlimit(0.0, 1.0, (db - kMiniSpectrumFloorDb) / (kMiniSpectrumTopDb - kMiniSpectrumFloorDb));
                return area.getBottom() - static_cast<float>(share) * 0.92f * area.getHeight();
            };
            spectrum.startNewSubPath(map.freqToX(SpectrumDisplay::pointHz(0)), area.getBottom());
            for (int i = 0; i < count; ++i)
                spectrum.lineTo(map.freqToX(SpectrumDisplay::pointHz(i)), spectrumY(static_cast<double>(levels[static_cast<size_t>(i)])));
            spectrum.lineTo(map.freqToX(SpectrumDisplay::pointHz(count - 1)), area.getBottom());
            spectrum.closeSubPath();
        }

        // No finer than a point for every two pixels.
        const int points = std::max(24, juce::roundToInt(area.getWidth() * 0.5f));
        for (int i = 0; i < points; ++i) {
            const double hz = logSpacedHz(i, points);
            const juce::Point<float> at{map.freqToX(hz), lineY(dsp::DeEssModule::responseDb(hz, settings_.frequencyHz, settings_.mode, reductionDb_, rate))};
            if (i == 0)
                line.startNewSubPath(at);
            else
                line.lineTo(at);
        }
    }

    g.setColour(accent.withAlpha(0.16f * dim));
    g.fillRect(band);
    g.setColour(theme::spectrumBefore.withAlpha(0.30f * dim));
    g.fillPath(spectrum);
    g.setColour(accent.withAlpha(dim));
    g.strokePath(line, juce::PathStrokeType(kLineThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

}  // namespace smartq
