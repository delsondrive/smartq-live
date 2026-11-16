#include "ui/SongEditorLanes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "app/SongCache.h"
#include "ui/BarScale.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {
juce::Font smallFont() { return theme::font(theme::sizeSmall); }
juce::Font smallBoldFont() { return theme::font(theme::sizeSmall, true); }
}  // namespace

SongEditorPage::LanesBody::LanesBody(SongEditorPage& page) : page_(page) {}

int SongEditorPage::LanesBody::idealHeight() const { return static_cast<int>(page_.state_.parts.size()) * partRow + clickRow; }

void SongEditorPage::LanesBody::invalidate() {
    built_ = false;
    repaint();
}

void SongEditorPage::LanesBody::paint(juce::Graphics& g) {
    if (!page_.scale_ || !page_.doc_) return;
    if (!built_) build();                                         // a pixel's columns: the summaries are made already
    const auto clip = g.getClipBounds();
    const auto& parts = page_.state_.parts;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const juce::Rectangle<int> row(0, static_cast<int>(i) * partRow, getWidth(), partRow);
        if (!row.intersects(clip)) continue;
        const auto& part = parts[i];
        paintHeader(g, row, part.colour, part.name);
        const auto lane = row.withTrimmedLeft(headerColumn);
        paintLaneBackground(g, lane);
        const auto words = lane.reduced(10, 0);
        if (part.missing) {
            g.setColour(theme::caution);
            g.setFont(smallFont());
            g.drawText("The audio file is missing", words, juce::Justification::centredLeft, true);
        } else if (page_.state_.loading) {
            g.setColour(theme::dim);
            g.setFont(smallFont());
            g.drawText("Loading" + kEllipsis + " " + juce::String(juce::roundToInt(page_.state_.progress * 100.0)) + "%", words,
                       juce::Justification::centredLeft, true);
        } else if (const auto found = columns_.find(part.partId); found != columns_.end()) {
            paintWave(g, lane, found->second, part.colour);
        } else {
            g.setColour(theme::caution);
            g.setFont(smallFont());
            g.drawText("The audio file couldn't be read", words, juce::Justification::centredLeft, true);
        }
    }
    const juce::Rectangle<int> click(0, static_cast<int>(parts.size()) * partRow, getWidth(), clickRow);
    if (click.intersects(clip)) {
        paintHeader(g, click, theme::ember, "Click");
        const auto lane = click.withTrimmedLeft(headerColumn);
        paintLaneBackground(g, lane);
        paintTicks(g, lane);
    }
}

void SongEditorPage::LanesBody::mouseDown(const juce::MouseEvent& e) {
    pressedLane_ = e.x >= headerColumn;
    if (e.mods.isPopupMenu()) return;
    if (!pressedLane_) {
        const int row = e.y / partRow;
        if (e.y >= 0 && row < static_cast<int>(page_.state_.parts.size()))
            page_.showPartMenu(page_.state_.parts[static_cast<std::size_t>(row)].partId);
        return;
    }
    page_.pointerAt(e.position.x - static_cast<float>(headerColumn), e.mods);
}

void SongEditorPage::LanesBody::mouseDrag(const juce::MouseEvent& e) {
    if (pressedLane_ && !e.mods.isPopupMenu()) page_.pointerAt(e.position.x - static_cast<float>(headerColumn), e.mods);
}

void SongEditorPage::LanesBody::paintHeader(juce::Graphics& g, juce::Rectangle<int> row, juce::Colour dot, const juce::String& name) {
    const auto head = row.withWidth(headerColumn);
    g.setColour(juce::Colours::black.withAlpha(0.18f));
    g.fillRect(head);
    g.setColour(theme::hairline);
    g.fillRect(head.getRight() - 1, head.getY(), 1, head.getHeight());
    g.fillRect(head.getX(), head.getBottom() - 1, head.getWidth(), 1);
    const float cy = static_cast<float>(head.getCentreY());
    g.setColour(dot);
    g.fillEllipse(8.0f, cy - 4.0f, 8.0f, 8.0f);
    const auto text = juce::Rectangle<float>(22.0f, static_cast<float>(head.getY()), static_cast<float>(headerColumn - 22 - 8),
                                             static_cast<float>(head.getHeight()));
    g.setColour(theme::mid);
    g.setFont(smallBoldFont());
    g.drawText(shorten(name, smallBoldFont(), text.getWidth()), text, juce::Justification::centredLeft, false);
}

void SongEditorPage::LanesBody::paintLaneBackground(juce::Graphics& g, juce::Rectangle<int> lane) const {
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    for (const float x : barLines_) g.fillRect(static_cast<float>(lane.getX()) + x, static_cast<float>(lane.getY()), 1.0f,
                                               static_cast<float>(lane.getHeight() - 1));
    g.setColour(theme::hairline);
    g.fillRect(lane.getX(), lane.getBottom() - 1, lane.getWidth(), 1);
}

void SongEditorPage::LanesBody::paintWave(juce::Graphics& g, juce::Rectangle<int> lane, const Columns& c, juce::Colour colour) {
    const float mid = static_cast<float>(lane.getCentreY());
    const float half = static_cast<float>(partRow - 2 * waveInset) * 0.5f;
    const float scale = c.peak > 1.0e-6f ? 0.9f * half / c.peak : 0.0f;
    // The mockup's canvas.wf: a 1.3 px bar every 2 px, at 75 % opacity, so a dense part keeps its texture.
    g.setColour(colour.withMultipliedAlpha(0.75f));
    for (std::size_t x = 0; x < c.lo.size(); x += 2) {
        float lo = c.lo[x], hi = c.hi[x];
        if (x + 1 < c.lo.size() && !std::isnan(c.lo[x + 1])) {          // the bar covers both pixels' columns
            lo = std::isnan(lo) ? c.lo[x + 1] : std::min(lo, c.lo[x + 1]);
            hi = std::isnan(hi) ? c.hi[x + 1] : std::max(hi, c.hi[x + 1]);
        }
        if (std::isnan(lo)) continue;
        // From the bar's top to its bottom, always through the centre line: a bar wholly above or below zero still
        // reaches it.
        const float top = mid - hi * scale, bottom = mid - lo * scale;
        const float y = std::min(top, mid - 0.5f);
        g.fillRect(static_cast<float>(lane.getX()) + static_cast<float>(x), y, 1.3f, std::max(bottom, mid + 0.5f) - y);
    }
}

void SongEditorPage::LanesBody::paintTicks(juce::Graphics& g, juce::Rectangle<int> lane) const {
    const float mid = static_cast<float>(lane.getCentreY());
    const bool on = page_.state_.clickOn;
    for (const auto& tick : ticks_) {
        const float h = tick.kind == engine::ClickKind::Downbeat ? 24.0f : tick.kind == engine::ClickKind::Beat ? 12.0f : 6.0f;
        // Off, the click still sounds through the count-in, so only the count-in's ticks stay bright: those before it
        // (audio before bar 1 that is longer than the count-in) and those from bar 1 on are dimmed.
        g.setColour(on || tick.countIn ? theme::ember : theme::ember.withAlpha(0.35f));
        g.fillRect(static_cast<float>(lane.getX()) + tick.x, mid - h * 0.5f, 1.2f, h);
    }
}

void SongEditorPage::LanesBody::build() {
    built_ = true;
    columns_.clear();
    ticks_.clear();
    barLines_.clear();
    const auto& scale = *page_.scale_;
    const int width = static_cast<int>(scale.width());
    for (const int bar : scale.labelledBars(8.0f)) barLines_.push_back(std::round(scale.xAtBar(bar)));

    // The parts' columns, from the summaries the loader made of the audio that is loaded now.
    const auto& loaded = page_.loaded_;
    if (loaded != nullptr) {
        const double rate = loaded->sampleRate;
        const double barOne = page_.doc_->audioStartSeconds;
        for (const auto& part : page_.state_.parts) {
            const auto found = loaded->waves.find(part.partId);
            if (found == loaded->waves.end()) continue;
            const auto& summary = found->second;
            const auto blocks = static_cast<std::int64_t>(summary.lo.size());
            Columns c;
            c.peak = summary.peak;
            c.lo.assign(static_cast<std::size_t>(std::max(0, width)), std::numeric_limits<float>::quiet_NaN());
            c.hi = c.lo;
            constexpr double kBlock = app::WaveSummary::blockFrames;
            for (int x = 0; x < width; ++x) {
                const auto frameAt = [&](float px) { return (scale.secondsAtX(px) + barOne) * rate; };
                const auto b0 = static_cast<std::int64_t>(std::floor(frameAt(static_cast<float>(x)) / kBlock));
                auto b1 = static_cast<std::int64_t>(std::ceil(frameAt(static_cast<float>(x + 1)) / kBlock));
                b1 = std::max(b1, b0 + 1);
                float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
                for (auto b = std::max<std::int64_t>(0, b0); b < std::min(b1, blocks); ++b) {
                    lo = std::min(lo, summary.lo[static_cast<std::size_t>(b)]);
                    hi = std::max(hi, summary.hi[static_cast<std::size_t>(b)]);
                }
                if (lo <= hi) {
                    c.lo[static_cast<std::size_t>(x)] = lo;
                    c.hi[static_cast<std::size_t>(x)] = hi;
                }
            }
            columns_.emplace(part.partId, std::move(c));
        }
    }

    // The click's ticks, at a millisecond's precision, which is far finer than a pixel.
    constexpr double kRate = 1000.0;
    const auto from = static_cast<engine::SamplePos>(std::llround(scale.startSeconds() * kRate));
    const auto to = static_cast<engine::SamplePos>(std::llround(scale.endSeconds() * kRate));
    const auto subdivision = static_cast<engine::Subdivision>(juce::jlimit(0, 3, page_.state_.subdivision));
    // The count-in is [-its length, 0), as prepareSong's countInStart; it is empty with no count-in.
    const int countInBars = std::max(0, page_.doc_->countInBars);
    const auto countInStart = static_cast<engine::SamplePos>(std::llround(scale.tempo().secondsAt({1 - countInBars, 1.0}) * kRate));
    for (const auto& tick : engine::buildClickTicks(scale.tempo(), kRate, from, to, subdivision))
        ticks_.push_back({scale.xAtSeconds(static_cast<double>(tick.at) / kRate), tick.kind, tick.at >= countInStart && tick.at < 0});
}

}  // namespace sqlive::ui
