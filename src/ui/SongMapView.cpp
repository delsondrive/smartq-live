#include "ui/SongMapView.h"

#include <algorithm>
#include <cmath>
#include <tuple>

#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's CSS and canvas: .wave (8 px corners, black at 25%, a hairline border), a bar every 2 px, 1.4 px wide, up to
// 90% of the half-height, mint at 90% where played and rgba(141,127,150,.6) after; .playhead (2 px white, an 8 px glow).
constexpr float kRadius = 8.0f, kBarStep = 2.0f, kBarWidth = 1.4f, kHeadroom = 0.9f, kHeadWidth = 2.0f;
constexpr int kGlow = 8;
const juce::Colour kRest = juce::Colour(141, 127, 150).withAlpha(0.6f);

// .lane (5 px corners, black at 20%, a hairline border), its label (6 px in, 11 px semibold, dim), and .mk (2 px down from
// the lane's inside, 16 px high, 4 px corners, 5 px either side of 10 px bold text in the near-black on the colour).
constexpr float kLaneRadius = 5.0f, kLabelLeft = 7.0f, kChipTop = 3.0f, kChipHeight = 16.0f, kChipPad = 5.0f, kChipRadius = 4.0f;
constexpr float kTick = 6.0f, kChipGap = 1.0f;

juce::Font labelFont() { return theme::font(theme::sizeCaption, true); }
juce::Font chipFont() { return theme::font(theme::sizeTag, true); }

}  // namespace

SongMapView::SongMapView(app::Session& session) : session_(session) {
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setInterceptsMouseClicks(false, false);
}

void SongMapView::refresh() {
    const auto now = session_.now();
    const bool playing = now.playingSongId.isNotEmpty();
    const auto id = playing ? now.playingSongId : now.nextSongId;

    // The span the map draws, and where the audio lies across it: the same as the chips' (mapSpanSeconds).
    double start = 0.0, end = std::max(1.0, now.lengthSeconds), from = 0.0, to = 1.0;
    if (const auto* song = id.isNotEmpty() ? session_.model().song(id) : nullptr) {
        std::tie(start, end) = mapSpanSeconds(*song);
        const double span = std::max(1.0e-9, end - start);
        from = std::clamp((-song->audioStartSeconds - start) / span, 0.0, 1.0);
        to = std::clamp((song->lengthSeconds - song->audioStartSeconds - start) / span, from, 1.0);
    }
    const double progress = playing && end > start ? std::clamp((now.seconds - start) / (end - start), 0.0, 1.0) : 0.0;

    bool all = false;
    if (id != songId_) {
        songId_ = id;
        all = true;
    }
    if (start != start_ || end != end_ || from != audioFrom_ || to != audioTo_) {
        start_ = start;
        end_ = end;
        audioFrom_ = from;
        audioTo_ = to;
        makeBars();
        all = true;
    }
    // The cache's word on the song: a lookup, no file is read. A new LoadedSong (the audio was loaded again) is drawn again.
    const auto loaded = id.isNotEmpty() ? session_.cache().loaded(id) : nullptr;
    if (loaded.get() != loaded_.lock().get() || (loaded == nullptr && !min_.empty())) {
        loaded_ = loaded;
        min_ = loaded != nullptr ? loaded->overviewMin : std::vector<float>();
        max_ = loaded != nullptr ? loaded->overviewMax : std::vector<float>();
        makeBars();
        all = true;
    }

    const float before = playheadX();
    progress_ = progress;
    const float after = playheadX();
    if (all) {
        repaint();
    } else if (std::abs(after - before) >= 0.5f) {
        // Only the stretch of waveform the playhead crossed, with its glow either side.
        const float left = std::min(before, after) - kGlow - 2.0f, right = std::max(before, after) + kHeadWidth + kGlow + 2.0f;
        repaint(juce::Rectangle<float>(left, 0.0f, right - left, static_cast<float>(waveHeight)).getSmallestIntegerContainer());
    }
}

bool SongMapView::setLanes(std::vector<ShowState::Lane> lanes) {
    if (lanes == lanes_) return false;
    const bool reshaped = lanes.size() != lanes_.size();
    lanes_ = std::move(lanes);
    layoutChips();
    repaint(0, waveHeight, getWidth(), std::max(0, getHeight() - waveHeight));
    return reshaped;
}

int SongMapView::idealHeight() const {
    const int count = static_cast<int>(lanes_.size());
    return waveHeight + (count == 0 ? 0 : lanesTop + count * laneHeight + (count - 1) * laneGap);
}

float SongMapView::xAt(double position01) const {
    const float inner = std::max(0.0f, static_cast<float>(getWidth()) - 2.0f - kHeadWidth);
    return 1.0f + static_cast<float>(std::clamp(position01, 0.0, 1.0)) * inner;
}

float SongMapView::playheadX() const { return xAt(progress_); }

juce::Rectangle<float> SongMapView::laneArea(std::size_t lane) const {
    return {0.0f, static_cast<float>(waveHeight + lanesTop + static_cast<int>(lane) * (laneHeight + laneGap)), static_cast<float>(getWidth()),
            static_cast<float>(laneHeight)};
}

void SongMapView::resized() {
    makeBars();
    layoutChips();
}

void SongMapView::makeBars() {
    bars_.clear();
    const int points = static_cast<int>(std::min(min_.size(), max_.size()));
    const float left = xAt(audioFrom_), width = xAt(audioTo_) + kHeadWidth - left, mid = 0.5f * static_cast<float>(waveHeight);
    if (points == 0 || width <= 0.0f) return;
    float peak = 0.0f;
    for (int i = 0; i < points; ++i) peak = std::max({peak, std::abs(min_[static_cast<std::size_t>(i)]), std::abs(max_[static_cast<std::size_t>(i)])});
    if (peak <= 1.0e-6f) return;
    const float scale = kHeadroom * (mid - 1.0f) / peak;
    for (float x = 0.0f; x < width; x += kBarStep) {
        const int from = std::clamp(static_cast<int>(x / width * static_cast<float>(points)), 0, points - 1);
        const int to = std::clamp(static_cast<int>((x + kBarStep) / width * static_cast<float>(points)), from + 1, points);
        float lo = 0.0f, hi = 0.0f;
        for (int i = from; i < to; ++i) {
            lo = std::min(lo, min_[static_cast<std::size_t>(i)]);
            hi = std::max(hi, max_[static_cast<std::size_t>(i)]);
        }
        bars_.push_back({left + x, mid - hi * scale, mid - lo * scale});
    }
}

void SongMapView::layoutChips() {
    chips_.clear();
    const auto font = chipFont();
    for (std::size_t l = 0; l < lanes_.size(); ++l) {
        const auto area = laneArea(l);
        auto& chips = chips_.emplace_back();
        for (const auto& chip : lanes_[l].chips)
            chips.push_back({std::round(xAt(chip.position01)), area.getY() + kChipTop, widthOf(font, chip.label) + 2.0f * kChipPad,
                             kChipHeight});
        // Each chip ends before the next one starts, down to a tick.
        for (std::size_t i = 0; i + 1 < chips.size(); ++i) {
            const float room = chips[i + 1].getX() - chips[i].getX() - kChipGap;
            if (room < chips[i].getWidth()) chips[i].setWidth(std::max(kTick, room));
        }
    }
}

void SongMapView::paint(juce::Graphics& g) {
    const auto clip = g.getClipBounds().toFloat();
    const auto wave = getLocalBounds().toFloat().withHeight(static_cast<float>(waveHeight));
    if (clip.intersects(wave)) {
        juce::Graphics::ScopedSaveState saved(g);
        g.setColour(juce::Colours::black.withAlpha(0.25f));
        g.fillRoundedRectangle(wave, kRadius);
        g.setColour(theme::hairline);
        g.drawRoundedRectangle(wave.reduced(0.5f), kRadius, 1.0f);
        if (songId_.isNotEmpty()) {
            juce::Path box;
            box.addRoundedRectangle(wave.reduced(1.0f), kRadius - 1.0f);
            g.reduceClipRegion(box);
            const auto inside = g.getClipBounds().toFloat();
            const float head = playheadX();
            const auto played = theme::mint.withAlpha(0.9f);
            for (const auto& bar : bars_) {
                if (bar.x + kBarWidth < inside.getX()) continue;
                if (bar.x > inside.getRight()) break;
                g.setColour(bar.x < head ? played : kRest);
                g.fillRect(bar.x, bar.top, kBarWidth, std::max(1.0f, bar.bottom - bar.top));
            }

            // The playhead and its glow, fading out over 8 px either side.
            for (int k = kGlow; k >= 1; --k) {
                g.setColour(juce::Colours::white.withAlpha(0.05f));
                g.fillRect(head - static_cast<float>(k), 0.0f, kHeadWidth + 2.0f * static_cast<float>(k), wave.getHeight());
            }
            g.setColour(juce::Colours::white);
            g.fillRect(head, 0.0f, kHeadWidth, wave.getHeight());
        }
    }
    if (clip.getBottom() > static_cast<float>(waveHeight)) paintLanes(g);
}

void SongMapView::paintLanes(juce::Graphics& g) {
    const auto chipText = chipFont(), label = labelFont();
    for (std::size_t l = 0; l < lanes_.size() && l < chips_.size(); ++l) {
        const auto area = laneArea(l);
        const auto& lane = lanes_[l];
        juce::Graphics::ScopedSaveState saved(g);
        g.setColour(juce::Colours::black.withAlpha(0.2f));
        g.fillRoundedRectangle(area, kLaneRadius);
        g.setColour(theme::hairline);
        g.drawRoundedRectangle(area.reduced(0.5f), kLaneRadius, 1.0f);
        juce::Path box;
        box.addRoundedRectangle(area.reduced(1.0f), kLaneRadius - 1.0f);
        g.reduceClipRegion(box);

        g.setColour(theme::dim);
        g.setFont(label);
        g.drawText(lane.name, area.withTrimmedLeft(kLabelLeft), juce::Justification::centredLeft, false);

        for (std::size_t i = 0; i < chips_[l].size() && i < lane.chips.size(); ++i) {
            const auto chip = chips_[l][i];
            g.setColour(lane.colour);
            g.fillRoundedRectangle(chip, std::min(kChipRadius, chip.getWidth() / 2.0f));
            const auto words = shorten(lane.chips[i].label, chipText, chip.getWidth() - 2.0f * kChipPad);
            if (words.isEmpty()) continue;
            g.setColour(theme::onAccent);
            g.setFont(chipText);
            g.drawText(words, chip.reduced(kChipPad, 0.0f), juce::Justification::centredLeft, false);
        }
    }
}

}  // namespace sqlive::ui
