#include "engine/PreparedSong.h"

#include <algorithm>
#include <utility>

namespace sqlive::engine {

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
    return s;
}

} // namespace sqlive::engine
