#include "engine/PreparedSong.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sqlive::engine {

namespace {

constexpr float kSilentDb = -60.0f;   // this and below is 0

float gainOfDb(double db) noexcept {
    if (db <= kSilentDb) return 0.0f;
    if (db == 0.0) return 1.0f;
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

} // namespace

float automationGain(std::span<const AutoPoint> points, SamplePos at) noexcept {
    if (points.empty()) return 1.0f;
    const auto after = std::upper_bound(points.begin(), points.end(), at, [](SamplePos t, const AutoPoint& p) { return t < p.at; });
    return automationGainIn(points, (after - points.begin()) - 1, at);
}

bool automationFlat(std::span<const AutoPoint> points, std::ptrdiff_t last) noexcept {
    const auto n = static_cast<std::ptrdiff_t>(points.size());
    return last < 0 || last + 1 >= n || points[static_cast<std::size_t>(last)].db == points[static_cast<std::size_t>(last) + 1].db;
}

float automationGainIn(std::span<const AutoPoint> points, std::ptrdiff_t last, SamplePos at) noexcept {
    if (points.empty()) return 1.0f;
    const AutoPoint& a = points[static_cast<std::size_t>(std::max<std::ptrdiff_t>(last, 0))];
    if (automationFlat(points, last)) return gainOfDb(a.db);
    const AutoPoint& b = points[static_cast<std::size_t>(last) + 1];   // later than at, so b.at > a.at
    const double along = static_cast<double>(at - a.at) / static_cast<double>(b.at - a.at);
    return gainOfDb(static_cast<double>(a.db) + (static_cast<double>(b.db) - static_cast<double>(a.db)) * along);
}

SamplePos countOffFloor(const TempoMap& tempo, double sampleRate, SamplePos start) {
    return std::min(start, tempo.sampleAt({0, 1.0}, sampleRate));
}

PreparedSong prepareSong(std::string id, TempoMap tempo, int countInBars, SamplePos audioStartOffset,
                         std::vector<PreparedPart> parts, ClickSetup click, double sampleRate, SamplePos explicitEnd) {
    PreparedSong s;
    s.id = std::move(id);
    s.sampleRate = sampleRate;
    s.tempo = std::move(tempo);
    s.audioStartOffset = audioStartOffset;

    const SamplePos countIn = -s.tempo.sampleAt({1 - std::max(0, countInBars), 1.0}, sampleRate);
    s.start = -std::max(countIn, audioStartOffset);
    s.bodyStart = -audioStartOffset;
    s.countInStart = -countIn;

    SamplePos end = explicitEnd;
    if (end < 0) {
        SamplePos longest = 0;
        for (const auto& p : parts)
            if (p.audio) longest = std::max(longest, static_cast<SamplePos>(p.audio->left.size()));
        end = longest - audioStartOffset;
    }
    s.end = std::max<SamplePos>(0, end);

    s.parts = std::move(parts);
    s.click = click;
    // Always built: a song with its click off still clicks through its count-in. From the bar before bar 1 when that is
    // earlier than the start, so a step at bar 1 still has a bar of ticks for startAtStep's count-off; ordinary playback
    // never reaches them.
    s.ticks = buildClickTicks(s.tempo, sampleRate, countOffFloor(s.tempo, sampleRate, s.start), s.end, click.subdivision);
    s.clickSound = makeClickSound(click.sound, sampleRate);
    finishSections(s);
    return s;
}

void finishSections(PreparedSong& s) {
    if (s.sections.empty()) s.sections.push_back({0, s.end});
    const auto count = s.sections.size();
    std::erase_if(s.arrangement, [&](std::uint16_t step) { return step >= count; });
    if (s.arrangement.empty())
        for (std::size_t i = 0; i < count && i <= 0xFFFF; ++i) s.arrangement.push_back(static_cast<std::uint16_t>(i));

    s.barStarts.clear();
    if (!(s.sampleRate > 0.0)) return;
    for (int bar = 1;; ++bar) {
        const SamplePos at = s.tempo.sampleAt({bar, 1.0}, s.sampleRate);
        if (at >= s.end) break;
        s.barStarts.push_back(at);
    }
}

} // namespace sqlive::engine
