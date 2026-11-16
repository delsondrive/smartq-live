#include "ui/SongMapView.h"

#include <algorithm>
#include <cmath>

#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's CSS and canvas: .wave (8 px corners, black at 25%, a hairline border), a bar every 2 px, 1.4 px wide, up to
// 90% of the half-height, mint at 90% where played and rgba(141,127,150,.6) after; .playhead (2 px white, an 8 px glow).
constexpr float kRadius = 8.0f, kBarStep = 2.0f, kBarWidth = 1.4f, kHeadroom = 0.9f, kHeadWidth = 2.0f;
constexpr int kGlow = 8;
const juce::Colour kRest = juce::Colour(141, 127, 150).withAlpha(0.6f);

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
    const double progress = playing && now.lengthSeconds > 0.0 ? std::clamp(now.seconds / now.lengthSeconds, 0.0, 1.0) : 0.0;

    bool all = false;
    if (id != songId_) {
        songId_ = id;
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
        // Only the stretch the playhead crossed, with its glow either side.
        const float from = std::min(before, after) - kGlow - 2.0f, to = std::max(before, after) + kHeadWidth + kGlow + 2.0f;
        repaint(juce::Rectangle<float>(from, 0.0f, to - from, static_cast<float>(getHeight())).getSmallestIntegerContainer());
    }
}

float SongMapView::playheadX() const {
    const float inner = std::max(0.0f, static_cast<float>(getWidth()) - 2.0f - kHeadWidth);
    return 1.0f + static_cast<float>(progress_) * inner;
}

void SongMapView::resized() { makeBars(); }

void SongMapView::makeBars() {
    bars_.clear();
    const int points = static_cast<int>(std::min(min_.size(), max_.size()));
    const float width = static_cast<float>(getWidth()) - 2.0f, mid = 0.5f * static_cast<float>(getHeight());
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
        bars_.push_back({1.0f + x, mid - hi * scale, mid - lo * scale});
    }
}

void SongMapView::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.fillRoundedRectangle(r, kRadius);
    g.setColour(theme::hairline);
    g.drawRoundedRectangle(r.reduced(0.5f), kRadius, 1.0f);
    if (songId_.isEmpty()) return;

    juce::Path box;
    box.addRoundedRectangle(r.reduced(1.0f), kRadius - 1.0f);
    g.reduceClipRegion(box);
    const auto clip = g.getClipBounds().toFloat();
    const float head = playheadX();
    const auto played = theme::mint.withAlpha(0.9f);
    for (const auto& bar : bars_) {
        if (bar.x + kBarWidth < clip.getX()) continue;
        if (bar.x > clip.getRight()) break;
        g.setColour(bar.x < head ? played : kRest);
        g.fillRect(bar.x, bar.top, kBarWidth, std::max(1.0f, bar.bottom - bar.top));
    }

    // The playhead and its glow, fading out over 8 px either side.
    for (int k = kGlow; k >= 1; --k) {
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.fillRect(head - static_cast<float>(k), 0.0f, kHeadWidth + 2.0f * static_cast<float>(k), r.getHeight());
    }
    g.setColour(juce::Colours::white);
    g.fillRect(head, 0.0f, kHeadWidth, r.getHeight());
}

}  // namespace sqlive::ui
