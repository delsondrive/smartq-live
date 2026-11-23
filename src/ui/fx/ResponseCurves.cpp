#include "ui/fx/ResponseCurves.h"

#include <algorithm>
#include <cmath>

namespace smartq {

namespace {

size_t idx(int n) { return static_cast<size_t>(n); }

bool sameResponse(const dsp::BandSettings& a, const dsp::BandSettings& b) {
    if (a.enabled != b.enabled) return false;
    if (!a.enabled) return true;
    return a.shape == b.shape && a.frequencyHz == b.frequencyHz && a.gainDb == b.gainDb && a.q == b.q
           && a.slopeDbPerOct == b.slopeDbPerOct;
}

double designedCentre(const dsp::BandSettings& s, double sampleRate) {
    return std::min(s.frequencyHz, dsp::kMaxFrequencyRatio * sampleRate);
}

}  // namespace

ResponseCurves::ResponseCurves() {
    fullGrid_.resize(idx(kGridPoints));
    for (int i = 0; i < kGridPoints; ++i) fullGrid_[idx(i)] = logSpacedHz(i, kGridPoints);
}

void ResponseCurves::rebuildGrid() {
    const double nyquist = 0.5 * sampleRate_;
    grid_.clear();
    for (const double hz : fullGrid_)
        if (hz < nyquist) grid_.push_back(hz);
    if (nyquist <= kGraphMaxHz) grid_.push_back(nyquist);
}

void ResponseCurves::computeBand(size_t band) {
    const auto& s = bands_[band];
    auto& curve = bandCurves_[band];
    auto& gridDb = gridDb_[band];
    curve.clear();
    gridDb.clear();
    if (!s.enabled) return;

    ++bandsRecomputed_;
    auto& sections = sections_[band];
    sections = dsp::designBand(s, s.gainDb, sampleRate_);

    const double centre = designedCentre(s, sampleRate_);
    bool centrePlaced = false;
    for (const double hz : grid_) {
        const auto db = static_cast<float>(dsp::digitalResponseDb(sections, hz, sampleRate_));
        gridDb.push_back(db);
        if (!centrePlaced && centre <= hz) {
            if (centre < hz)
                curve.push_back({centre, static_cast<float>(dsp::digitalResponseDb(sections, centre, sampleRate_))});
            centrePlaced = true;
        }
        curve.push_back({hz, db});
    }
}

void ResponseCurves::computeCurves() {
    centres_.clear();
    for (const auto& s : bands_)
        if (dsp::sounds(s)) centres_.push_back(designedCentre(s, sampleRate_));
    std::sort(centres_.begin(), centres_.end());

    // The shared frequencies: the grid, with the band centres slotted in. gridIndex_ is -1 for a centre.
    points_.clear();
    gridIndex_.clear();
    size_t nextCentre = 0;
    for (size_t i = 0; i < grid_.size(); ++i) {
        const double hz = grid_[i];
        for (; nextCentre < centres_.size() && centres_[nextCentre] <= hz; ++nextCentre) {
            const double centre = centres_[nextCentre];
            if (centre < hz && (points_.empty() || points_.back() < centre)) {
                points_.push_back(centre);
                gridIndex_.push_back(-1);
            }
        }
        points_.push_back(hz);
        gridIndex_.push_back(static_cast<int>(i));
    }

    // What the bands of each channel setting add up to.
    for (auto& sum : sums_) sum.assign(points_.size(), 0.0);
    bool anyLeftRight = false, anyMidSide = false;
    for (size_t band = 0; band < bands_.size(); ++band) {
        const auto& s = bands_[band];
        if (!dsp::sounds(s)) continue;

        const auto channel = mono_ ? dsp::ChannelMode::Stereo : s.channel;
        anyLeftRight = anyLeftRight || channel == dsp::ChannelMode::Left || channel == dsp::ChannelMode::Right;
        anyMidSide = anyMidSide || channel == dsp::ChannelMode::Mid || channel == dsp::ChannelMode::Side;

        auto& sum = sums_[static_cast<size_t>(channel)];
        for (size_t k = 0; k < points_.size(); ++k)
            sum[k] += gridIndex_[k] >= 0 ? static_cast<double>(gridDb_[band][idx(gridIndex_[k])])
                                         : dsp::digitalResponseDb(sections_[band], points_[k], sampleRate_);
    }

    const auto stereo = static_cast<size_t>(dsp::ChannelMode::Stereo);
    for (size_t path = 0; path < curves_.size(); ++path) {
        auto& curve = curves_[path];
        curve.clear();
        for (size_t k = 0; k < points_.size(); ++k) {
            const double db = path == stereo ? sums_[stereo][k] : sums_[stereo][k] + sums_[path][k];
            curve.push_back({points_[k], static_cast<float>(db)});
        }
    }

    // A band on one side of a pair says something about the other side too: that it is left alone.
    shown_.fill(false);
    shown_[stereo] = !anyLeftRight && !anyMidSide;
    shown_[static_cast<size_t>(dsp::ChannelMode::Left)] = anyLeftRight;
    shown_[static_cast<size_t>(dsp::ChannelMode::Right)] = anyLeftRight;
    shown_[static_cast<size_t>(dsp::ChannelMode::Mid)] = anyMidSide;
    shown_[static_cast<size_t>(dsp::ChannelMode::Side)] = anyMidSide;
}

bool ResponseCurves::update(const BandArray& bands, double sampleRate, bool mono) {
    bandsRecomputed_ = 0;
    const bool rateChanged = sampleRate != sampleRate_;
    if (rateChanged) {
        sampleRate_ = sampleRate;
        rebuildGrid();
    }

    bool changed = rateChanged || mono != mono_;
    mono_ = mono;
    for (size_t band = 0; band < bands.size(); ++band) {
        const bool same = sameResponse(bands_[band], bands[band]);
        const bool sameChannel = !bands[band].enabled || bands_[band].channel == bands[band].channel;
        const bool sameSound = dsp::sounds(bands_[band]) == dsp::sounds(bands[band]);  // a switch that leaves the band's own curve as it was
        bands_[band] = bands[band];
        if (!same || rateChanged) computeBand(band);
        changed = changed || !same || !sameChannel || !sameSound;
    }
    if (changed) computeCurves();
    return changed;
}

}  // namespace smartq
