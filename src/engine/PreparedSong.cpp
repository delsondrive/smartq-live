#include "engine/PreparedSong.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "engine/MidiClock.h"

namespace sqlive::engine {

namespace {

constexpr float kSilentDb = -60.0f;   // this and below is 0

float gainOfDb(double db) noexcept {
    if (db <= kSilentDb) return 0.0f;
    if (db == 0.0) return 1.0f;
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

// The bar `at` falls in (bar 1 at the earliest), and how long it is.
std::pair<int, SamplePos> barHolding(const PreparedSong& s, SamplePos at) noexcept {
    const auto& bars = s.barStarts;   // bar n starts at bars[n - 1]
    const int bar = std::max(1, static_cast<int>(std::upper_bound(bars.begin(), bars.end(), at) - bars.begin()));
    const SamplePos length = s.tempo.sampleAt({bar + 1, 1.0}, s.sampleRate) - s.tempo.sampleAt({bar, 1.0}, s.sampleRate);
    return {bar, std::max<SamplePos>(0, length)};
}

// The bar the section starts in, its click ticks and clock ticks moved one bar earlier.
CountOff makeCountOff(const PreparedSong& s, SamplePos at) {
    CountOff off;
    off.at = at;
    const auto [bar, length] = barHolding(s, at);
    off.length = length;
    const SamplePos from = s.tempo.sampleAt({bar, 1.0}, s.sampleRate);
    off.ticks = buildClickTicks(s.tempo, s.sampleRate, from, from + length, s.click.subdivision);
    off.clockTicks = buildClockTicks(s.tempo, s.sampleRate, from, from + length);
    const SamplePos shift = at - length - from;   // the bar's first sample lands where the count-off begins
    for (ClickTick& tick : off.ticks) tick.at += shift;
    for (SamplePos& tick : off.clockTicks) tick += shift;
    return off;
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

std::int64_t jumpFadeLength(double sampleRate) noexcept { return std::llround(240.0 * sampleRate / 48000.0); }

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
    // Always built: a song with its click off still clicks through its count-in.
    s.ticks = buildClickTicks(s.tempo, sampleRate, s.start, s.end, click.subdivision);
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
    // In time order, as the audio thread assumes: cues at the same time keep their order.
    std::stable_sort(s.cues.begin(), s.cues.end(), [](const CueEvent& a, const CueEvent& b) { return a.at < b.at; });
    if (s.sectionCue.size() != count) s.sectionCue.assign(count, nullptr);
    if (s.announceAt.size() != count) {
        s.announceAt.clear();
        for (const Section& section : s.sections) s.announceAt.push_back(section.end);
    }

    s.barStarts.clear();
    s.countOffs.clear();
    if (!(s.sampleRate > 0.0)) return;
    for (int bar = 1;; ++bar) {
        const SamplePos at = s.tempo.sampleAt({bar, 1.0}, s.sampleRate);
        if (at >= s.end) break;
        s.barStarts.push_back(at);
    }
    for (const Section& section : s.sections) s.countOffs.push_back(makeCountOff(s, section.start));
}

const CountOff* countOffAt(const PreparedSong& s, SamplePos at) noexcept {
    for (const CountOff& off : s.countOffs)
        if (off.at == at) return &off;
    return nullptr;
}

SamplePos countOffLength(const PreparedSong& s, SamplePos at) noexcept {
    if (const CountOff* off = countOffAt(s, at)) return off->length;
    return barHolding(s, at).second;
}

} // namespace sqlive::engine
