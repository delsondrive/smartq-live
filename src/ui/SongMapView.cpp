#include "ui/SongMapView.h"

#include <algorithm>
#include <cmath>
#include <tuple>

#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/SectionTiles.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's CSS and canvas: .wave (8 px corners, black at 25%, a hairline border), a bar every 2 px, 1.4 px wide, up to
// 90% of the half-height, mint at 90% where played and rgba(141,127,150,.6) after; .playhead (2 px white, an 8 px glow).
constexpr float kRadius = 8.0f, kBarStep = 2.0f, kBarWidth = 1.4f, kHeadroom = 0.9f, kHeadWidth = 2.0f;
constexpr int kGlow = 8;
const juce::Colour kRest = juce::Colour(141, 127, 150).withAlpha(0.6f);

// .lane (5 px corners, black at 20%, a hairline border), its label (6 px in, 11 px semibold, dim), and .mk (2 px down from
// the lane's inside, 16 px high, 4 px corners, 5 px either side of 10 px bold text in the near-black on the colour);
// .mk.auto (no fill, a 1 px dashed dim border, dim words).
constexpr float kLaneRadius = 5.0f, kLabelLeft = 7.0f, kChipTop = 3.0f, kChipHeight = 16.0f, kChipPad = 5.0f, kChipRadius = 4.0f;
constexpr float kTick = 6.0f, kChipGap = 1.0f, kLabelGap = 6.0f;
const float kAutoDashes[] = {3.0f, 2.0f};

juce::Font labelFont() { return theme::font(theme::sizeCaption, true); }
juce::Font chipFont() { return theme::font(theme::sizeTag, true); }

}  // namespace

SongMapView::SongMapView(app::Session& session) : session_(session) {
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setInterceptsMouseClicks(true, false);                        // hitTest: the blocks only
    setMouseCursor(juce::MouseCursor::PointingHandCursor);        // shown only where a click chooses
}

void SongMapView::refresh() {
    const auto now = session_.now();
    const bool playing = now.playingSongId.isNotEmpty();
    const auto id = playing ? now.playingSongId : now.nextSongId;

    // A single step: the span the map draws, and where the audio lies across it, as the chips' (mapSpanSeconds).
    double start = 0.0, end = std::max(1.0, now.lengthSeconds), from = 0.0, to = 1.0, audioStart = 0.0, audioLength = 0.0;
    if (const auto* song = id.isNotEmpty() ? session_.model().song(id) : nullptr) {
        std::tie(start, end) = mapSpanSeconds(*song);
        const double span = std::max(1.0e-9, end - start);
        from = std::clamp((-song->audioStartSeconds - start) / span, 0.0, 1.0);
        to = std::clamp((song->lengthSeconds - song->audioStartSeconds - start) / span, from, 1.0);
        audioStart = song->audioStartSeconds;
        audioLength = song->lengthSeconds;
    }
    // Sections: the steps laid end to end, as the tiles and the chips are. While the steps aren't known for a moment (the
    // song being prepared again) the ones the map has stay.
    const auto steps = session_.arrangement().steps;
    auto spans = steps.size() > 1 ? stepSpansOf(steps) : StepSpans{};
    if (steps.empty() && id == songId_) spans = spans_;

    double progress = 0.0;
    if (playing && !spans.steps.empty())
        progress = now.arrangedLengthSeconds > 0.0 ? std::clamp(now.arrangedSeconds / now.arrangedLengthSeconds, 0.0, 1.0) : 0.0;
    else if (playing && end > start)
        progress = std::clamp((now.seconds - start) / (end - start), 0.0, 1.0);

    bool all = false;
    if (id != songId_) {
        songId_ = id;
        all = true;
    }
    playing_ = playing && now.playing;
    if (start != start_ || end != end_ || from != audioFrom_ || to != audioTo_ || audioStart != audioStart_ || audioLength != audioLength_
        || !(spans == spans_)) {
        start_ = start;
        end_ = end;
        audioFrom_ = from;
        audioTo_ = to;
        audioStart_ = audioStart;
        audioLength_ = audioLength;
        spans_ = std::move(spans);
        makeBars();
        layoutBlocks();
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
        const auto wave = waveArea();
        const float left = std::min(before, after) - kGlow - 2.0f, right = std::max(before, after) + kHeadWidth + kGlow + 2.0f;
        repaint(juce::Rectangle<float>(left, wave.getY(), right - left, wave.getHeight()).getSmallestIntegerContainer());
    }
}

bool SongMapView::setSections(std::vector<ShowState::Tile> tiles, const juce::String& playingWords) {
    if (tiles == tiles_ && playingWords == playingWords_) return false;
    const bool reshaped = tiles.empty() != tiles_.empty();
    tiles_ = std::move(tiles);
    playingWords_ = playingWords;
    if (reshaped) {
        resized();                                                // everything under the row moves
        repaint();
    } else {
        layoutBlocks();
        repaint(0, 0, getWidth(), sectionsShown());
    }
    return reshaped;
}

bool SongMapView::setLanes(std::vector<ShowState::Lane> lanes) {
    if (lanes == lanes_) return false;
    const bool reshaped = lanes.size() != lanes_.size();
    lanes_ = std::move(lanes);
    layoutChips();
    const int top = static_cast<int>(waveArea().getBottom());
    repaint(0, top, getWidth(), std::max(0, getHeight() - top));
    return reshaped;
}

int SongMapView::sectionsShown() const { return tiles_.empty() ? 0 : sectionsTop + sectionsHeight + sectionsGap; }

int SongMapView::idealHeight() const {
    const int count = static_cast<int>(lanes_.size());
    return sectionsShown() + waveHeight + (count == 0 ? 0 : lanesTop + count * laneHeight + (count - 1) * laneGap);
}

int SongMapView::heightWithin(int room) const {
    const int count = static_cast<int>(lanes_.size());
    const int fit = std::clamp((room - sectionsShown() - waveHeight - lanesTop + laneGap) / (laneHeight + laneGap), 0, count);
    return sectionsShown() + waveHeight + (fit == 0 ? 0 : lanesTop + fit * laneHeight + (fit - 1) * laneGap);
}

int SongMapView::lanesShown() const {
    int shown = 0;
    while (shown < static_cast<int>(lanes_.size())
           && laneArea(static_cast<std::size_t>(shown)).getBottom() <= static_cast<float>(getHeight()))
        ++shown;
    return shown;
}

juce::Rectangle<float> SongMapView::waveArea() const {
    const float width = std::max(0.0f, static_cast<float>(getWidth() - 2 * sideBleed));
    return {static_cast<float>(sideBleed), static_cast<float>(sectionsShown()), width, static_cast<float>(waveHeight)};
}

float SongMapView::xAt(double position01) const {
    const auto wave = waveArea();
    const float inner = std::max(0.0f, wave.getWidth() - 2.0f - kHeadWidth);
    return wave.getX() + 1.0f + static_cast<float>(std::clamp(position01, 0.0, 1.0)) * inner;
}

float SongMapView::playheadX() const { return xAt(progress_); }

juce::Rectangle<float> SongMapView::laneArea(std::size_t lane) const {
    const auto wave = waveArea();
    const float y = wave.getBottom() + static_cast<float>(lanesTop + static_cast<int>(lane) * (laneHeight + laneGap));
    return {wave.getX(), y, wave.getWidth(), static_cast<float>(laneHeight)};
}

void SongMapView::resized() {
    makeBars();
    layoutBlocks();
    layoutChips();
}

void SongMapView::makeBars() {
    bars_.clear();
    const int points = static_cast<int>(std::min(min_.size(), max_.size()));
    const auto wave = waveArea();
    const float mid = wave.getCentreY();
    if (points == 0 || wave.getWidth() <= 0.0f) return;
    float peak = 0.0f;
    for (int i = 0; i < points; ++i) peak = std::max({peak, std::abs(min_[static_cast<std::size_t>(i)]), std::abs(max_[static_cast<std::size_t>(i)])});
    if (peak <= 1.0e-6f) return;
    const float scale = kHeadroom * (0.5f * static_cast<float>(waveHeight) - 1.0f) / peak;
    const auto bar = [&](float x, int from, int to) {
        from = std::clamp(from, 0, points - 1);
        to = std::clamp(to, from + 1, points);
        float lo = 0.0f, hi = 0.0f;
        for (int i = from; i < to; ++i) {
            lo = std::min(lo, min_[static_cast<std::size_t>(i)]);
            hi = std::max(hi, max_[static_cast<std::size_t>(i)]);
        }
        bars_.push_back({x, mid - hi * scale, mid - lo * scale});
    };

    if (spans_.steps.empty() || spans_.total <= 0.0) {
        // A single step: the audio where it lies across the span.
        const float left = xAt(audioFrom_), width = xAt(audioTo_) + kHeadWidth - left;
        if (width <= 0.0f) return;
        for (float x = 0.0f; x < width; x += kBarStep)
            bar(left + x, static_cast<int>(x / width * static_cast<float>(points)),
                static_cast<int>((x + kBarStep) / width * static_cast<float>(points)));
        return;
    }
    // Sections: step by step, each step's stretch of the audio in turn.
    if (audioLength_ <= 0.0) return;
    const float left = xAt(0.0), width = xAt(1.0) + kHeadWidth - left;
    const auto pointAt = [&](double songSeconds) { return static_cast<int>((songSeconds + audioStart_) / audioLength_ * points); };
    std::size_t step = 0;
    for (float x = 0.0f; x < width; x += kBarStep) {
        const double along = static_cast<double>(x / width) * spans_.total;
        const double until = static_cast<double>((x + kBarStep) / width) * spans_.total;
        while (step + 1 < spans_.steps.size() && spans_.steps[step + 1].at <= along) ++step;
        const auto& s = spans_.steps[step];
        const double from = s.songStart + (along - s.at), to = std::min(s.songEnd, s.songStart + (until - s.at));
        if (from + audioStart_ < 0.0 || from + audioStart_ >= audioLength_) continue;   // no audio there
        bar(left + x, pointAt(from), pointAt(to));
    }
}

void SongMapView::layoutBlocks() {
    blocks_.clear();
    if (tiles_.empty()) return;
    // Each as wide as its step is long (by its bars, while the steps' times aren't known), 3 px apart.
    const bool timed = spans_.steps.size() == tiles_.size() && spans_.total > 0.0;
    double total = 0.0;
    for (const auto& tile : tiles_) total += std::max(1, tile.bars);
    double along = 0.0;
    const float y = static_cast<float>(sectionsTop), gap = sectionTiles::blockGap;
    for (std::size_t i = 0; i < tiles_.size(); ++i) {
        const double length = timed ? spans_.steps[i].songEnd - spans_.steps[i].songStart
                                    : static_cast<double>(std::max(1, tiles_[i].bars));
        const double from = (timed ? spans_.steps[i].at : along) / (timed ? spans_.total : total);
        const double to = from + length / (timed ? spans_.total : total);
        along += length;
        const float left = std::round(xAt(from)), right = std::round(i + 1 == tiles_.size() ? xAt(1.0) + kHeadWidth : xAt(to) - gap);
        blocks_.push_back({left, y, std::max(1.0f, right - left), static_cast<float>(sectionsHeight)});
    }
}

int SongMapView::blockAt(juce::Point<float> at) const {
    for (std::size_t i = 0; i < blocks_.size(); ++i)
        if (blocks_[i].contains(at)) return static_cast<int>(i);
    return -1;
}

bool SongMapView::hitTest(int x, int y) {
    const int step = blockAt({static_cast<float>(x), static_cast<float>(y)});
    return playing_ && step >= 0 && step < 10;
}

void SongMapView::mouseUp(const juce::MouseEvent& e) {
    const int step = blockAt(e.position);
    if (!playing_ || step < 0 || step >= 10 || e.mods.isPopupMenu()) return;
    if (blockAt(e.mouseDownPosition) != step) return;             // pressed on one, let go on another: nothing
    session_.perform(static_cast<app::Action>(static_cast<int>(app::Action::Section1) + step));
}

void SongMapView::layoutChips() {
    chips_.clear();
    const auto font = chipFont();
    for (std::size_t l = 0; l < lanes_.size(); ++l) {
        const auto area = laneArea(l);
        auto& chips = chips_.emplace_back();
        // Never over the lane's name: a chip at the very start (bar 1, with sections) begins just after it.
        const float first = std::ceil(area.getX() + kLabelLeft + widthOf(labelFont(), lanes_[l].name) + kLabelGap);
        for (const auto& chip : lanes_[l].chips)
            chips.push_back({std::max(first, std::round(xAt(chip.position01))), area.getY() + kChipTop,
                             widthOf(font, chip.label) + 2.0f * kChipPad, kChipHeight});
        // Each chip ends before the next one starts, down to a tick.
        for (std::size_t i = 0; i + 1 < chips.size(); ++i) {
            const float room = chips[i + 1].getX() - chips[i].getX() - kChipGap;
            if (room < chips[i].getWidth()) chips[i].setWidth(std::max(kTick, room));
        }
    }
}

void SongMapView::paint(juce::Graphics& g) {
    const auto clip = g.getClipBounds().toFloat();

    // The section blocks: "Chorus · 6 bars left" on the one playing.
    if (!blocks_.empty() && clip.getY() < static_cast<float>(sectionsShown())) {
        using Kind = ShowState::Tile::Kind;
        for (std::size_t i = 0; i < blocks_.size() && i < tiles_.size(); ++i) {
            const auto& tile = tiles_[i];
            const auto words = tile.kind == Kind::Now && playingWords_.isNotEmpty() ? playingWords_ : tile.name;
            sectionTiles::paintBlock(g, blocks_[i], tile, i, words);
        }
    }

    const auto wave = waveArea();
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
                g.fillRect(head - static_cast<float>(k), wave.getY(), kHeadWidth + 2.0f * static_cast<float>(k), wave.getHeight());
            }
            g.setColour(juce::Colours::white);
            g.fillRect(head, wave.getY(), kHeadWidth, wave.getHeight());
        }
    }
    if (clip.getBottom() > wave.getBottom()) paintLanes(g);
}

void SongMapView::paintLanes(juce::Graphics& g) {
    const auto chipText = chipFont(), label = labelFont();
    const auto shown = static_cast<std::size_t>(lanesShown());   // a lane that doesn't fit whole is left out
    for (std::size_t l = 0; l < shown && l < chips_.size(); ++l) {
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
            const bool automatic = lane.chips[i].automatic;
            const float radius = std::min(kChipRadius, chip.getWidth() / 2.0f);
            if (automatic) {                                     // .mk.auto: a dashed dim border, no fill
                juce::Path outline, dashed;
                outline.addRoundedRectangle(chip.reduced(0.5f), radius);
                juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, kAutoDashes, 2);
                g.setColour(theme::dim);
                g.fillPath(dashed);
            } else {
                g.setColour(lane.colour);
                g.fillRoundedRectangle(chip, radius);
            }
            const auto words = shorten(lane.chips[i].label, chipText, chip.getWidth() - 2.0f * kChipPad);
            if (words.isEmpty()) continue;
            g.setColour(automatic ? theme::dim : theme::onAccent);
            g.setFont(chipText);
            g.drawText(words, chip.reduced(kChipPad, 0.0f), juce::Justification::centredLeft, false);
        }
    }
}

}  // namespace sqlive::ui
