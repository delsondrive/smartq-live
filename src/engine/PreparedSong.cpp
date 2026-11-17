#include "engine/PreparedSong.h"

#include <algorithm>
#include <utility>

namespace sqlive::engine {

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
