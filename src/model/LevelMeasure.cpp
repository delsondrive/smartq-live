#include "model/LevelMeasure.h"

#include <algorithm>
#include <cmath>

#include "engine/Engine.h"
#include "engine/OutputEffects.h"
#include "model/Effects.h"
#include "model/Routing.h"

namespace sqlive::model {

namespace {

constexpr int kBlock = 4096;
constexpr int kBlocksBetweenAsks = 64;
constexpr int kLimitPiece = 256;   // the output effects play in pieces this long, each Limit read after each

// The first song time from songTime on where the playing step can hand over to another (a jump, or the lead-in's end), or
// -1 when none lies ahead. A block never runs past one, so every sample of it is at songTime + its place in the block.
engine::SamplePos nextHandOver(const engine::PreparedSong& song, int step, engine::SamplePos songTime) {
    if (step < 0) return songTime < 0 ? 0 : -1;
    const auto index = static_cast<std::size_t>(step);
    if (index >= song.arrangement.size() || song.arrangement[index] >= song.sections.size()) return -1;
    const auto end = song.sections[song.arrangement[index]].end;
    return songTime < end ? end : -1;
}

// How long the whole arrangement plays: its lead-in, then every step's section.
engine::SamplePos arrangementLength(const engine::PreparedSong& song) {
    engine::SamplePos length = -std::min<engine::SamplePos>(song.start, 0);
    for (const auto section : song.arrangement)
        if (section < song.sections.size()) length += song.sections[section].end - song.sections[section].start;
    return length;
}

struct Watch {
    int firstChannel = 0, channels = 0;   // the bus's device channels; none for an output with no bus
    int bus = -1;                         // its bus in the mix; -1 for none
    float peak = 0.0f;
    engine::SamplePos at = 0;             // the song time of its peak
    float limit = 0.0f;                   // the most its Limit took, in dB
    engine::SamplePos limitAt = 0;        // the song time of that
};

// Where in [at, at + frames) the output is loudest (the first such sample): where its Limit works hardest in that piece.
// Not the piece's start, which the Limit's lookahead begins turning down before the loud audio arrives.
int loudestIn(const Watch& w, const std::vector<std::vector<float>>& buffers, int at, int frames) {
    int loudest = at;
    float most = -1.0f;
    for (int i = at; i < at + frames; ++i)
        for (int c = w.firstChannel; c < w.firstChannel + w.channels; ++c)
            if (const float m = std::abs(buffers[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)]); m > most) {
                most = m;
                loudest = i;
            }
    return loudest;
}

}  // namespace

SongLevels measureSong(std::shared_ptr<const engine::PreparedSong> song, const BandDoc& band, const RigDoc& rig,
                       double sampleRate, const std::function<bool(double progress)>& keepGoing) {
    SongLevels levels;
    if (song == nullptr) return levels;
    levels.songId = juce::String(song->id);

    // Every output, in the rig's order, with the device channels its bus is on, as the mix builds it for the most channels
    // the engine renders. One that doesn't fit whole within them (a stereo pair on the last channel too) isn't
    // measured: the engine can't play all of it, and it reads as silence.
    std::vector<Watch> watches;
    int deviceChannels = 1;
    for (const auto& o : rig.outputs) {
        Watch w;
        const int channels = o.stereo ? 2 : 1;
        if (isMeasured(rig, o.id)) {
            w.firstChannel = o.firstChannel - 1;
            w.channels = channels;
            w.bus = busIndexOf(rig, o.id, engine::kMaxDeviceChannels);
            deviceChannels = std::max(deviceChannels, w.firstChannel + w.channels);
        }
        watches.push_back(w);
    }

    const auto mix = buildMixSetup(band, rig, engine::kMaxDeviceChannels);
    auto engine = std::make_unique<engine::Engine>();
    engine->prepare(sampleRate, kBlock);
    engine->setMix(mix);
    engine->cue(song);
    engine->start();
    // Each output's effects, played here on what the engine renders, as the engine plays them (a chain gives the same bits
    // for any split of the blocks), in pieces short enough to tell where a Limit works.
    const auto effects = buildOutputEffects(rig, *mix, sampleRate, kBlock);
    const int latency = effects != nullptr ? effects->latencySamples() : 0;

    std::vector<std::vector<float>> buffers(static_cast<std::size_t>(deviceChannels), std::vector<float>(kBlock, 0.0f));
    std::vector<float*> out, piece(buffers.size());
    for (auto& b : buffers) out.push_back(b.data());
    // The song time of what each sample of a block leaves with. Everything leaves `latency` samples late, so the first
    // `latency` are those of the end of the block before (the song's start before any), then come the block's own.
    std::vector<engine::SamplePos> timeline(static_cast<std::size_t>(latency + kBlock), song->start);

    // The block's n samples (in buffers) through the output effects, then measured; then the timeline moves on by n.
    const auto measure = [&](int n) {
        if (effects != nullptr)
            for (int at = 0; at < n; at += kLimitPiece) {
                const int m = std::min(kLimitPiece, n - at);
                for (std::size_t c = 0; c < buffers.size(); ++c) piece[c] = buffers[c].data() + at;
                effects->process(piece.data(), deviceChannels, m);
                for (auto& w : watches)
                    if (const float taken = effects->limitReductionDb(w.bus); taken > w.limit) {
                        w.limit = taken;
                        w.limitAt = timeline[static_cast<std::size_t>(loudestIn(w, buffers, at, m))];
                    }
            }
        for (auto& w : watches)
            for (int c = w.firstChannel; c < w.firstChannel + w.channels; ++c) {
                const float* samples = buffers[static_cast<std::size_t>(c)].data();
                for (int i = 0; i < n; ++i)
                    if (const float m = std::abs(samples[i]); m > w.peak) {
                        w.peak = m;
                        w.at = timeline[static_cast<std::size_t>(i)];
                    }
            }
        std::copy(timeline.begin() + n, timeline.begin() + n + latency, timeline.begin());
    };

    const engine::SamplePos expected = arrangementLength(*song);
    const engine::SamplePos most = expected + static_cast<engine::SamplePos>(std::llround(2.0 * sampleRate));
    engine::SamplePos played = 0;
    engine::SamplePos songTime = song->start;   // where the next block begins in the song
    int step = -1;
    bool stopped = false;
    const bool playable = song->sampleRate == sampleRate;   // the engine plays nothing made at another rate
    for (int block = 0; playable && played < most; ++block) {
        if (block > 0 && block % kBlocksBetweenAsks == 0 && keepGoing
            && !keepGoing(expected > 0 ? std::min(1.0, static_cast<double>(played) / static_cast<double>(expected)) : 1.0))
            break;
        engine::SamplePos frames = std::min<engine::SamplePos>(kBlock, most - played);
        if (const auto handOver = nextHandOver(*song, step, songTime); handOver > songTime)
            frames = std::min(frames, handOver - songTime);
        const int n = static_cast<int>(frames);
        engine->process(out.data(), deviceChannels, n);   // clears out first, so each block holds only its own samples
        played += n;

        // A block runs straight through the song unless it began with a jump (taken at its first sample). One the song
        // ended in reports no song time: it ran straight on to the end.
        const auto status = engine->status();
        const bool straight = status.state == engine::PlayState::Stopped || status.songTime - n == songTime;
        const engine::SamplePos first = straight ? songTime : status.songTime - n;
        for (int i = 0; i < n; ++i) timeline[static_cast<std::size_t>(latency + i)] = first + i;
        measure(n);
        songTime = status.songTime;
        step = status.step;

        // The engine holds back commands until what it lets go of is collected, and its queues would fill.
        engine::EngineEvent event;
        while (engine->popEvent(event)) {}
        engine::OutMidi midi;
        while (engine->popMidi(midi)) {}
        engine->collectGarbage();
        if (status.state == engine::PlayState::Stopped) {
            stopped = true;
            break;
        }
    }
    // What the output effects still hold of the song's end leaves after it: silence goes in behind it.
    for (int left = stopped ? latency : 0; left > 0;) {
        const int n = std::min(left, kBlock);
        for (auto& b : buffers) std::fill_n(b.begin(), n, 0.0f);
        std::fill_n(timeline.begin() + latency, n, timeline[static_cast<std::size_t>(latency - 1)]);
        measure(n);
        left -= n;
    }

    levels.complete = stopped;
    const auto barOf = [&song](engine::SamplePos at) {
        return at > 0 ? std::max(1, song->tempo.barBeatAt(static_cast<double>(at) / song->sampleRate).bar) : 1;
    };
    for (std::size_t i = 0; i < watches.size(); ++i) {
        const auto& w = watches[i];
        OutputPeak peak;
        peak.outputId = rig.outputs[i].id;
        if (w.peak > 0.0f) {
            peak.peakDb = std::max(-100.0f, 20.0f * std::log10(w.peak));
            peak.clips = w.peak > 1.0f;
            peak.bar = barOf(w.at);
        }
        if (w.limit > 0.0f) {
            peak.limitDb = w.limit;
            peak.limitBar = barOf(w.limitAt);
        }
        levels.outputs.push_back(peak);
    }
    return levels;
}

bool isMeasured(const RigDoc& rig, const juce::String& outputId) noexcept {
    if (busIndexOf(rig, outputId, engine::kMaxDeviceChannels) < 0) return false;
    for (const auto& o : rig.outputs)
        if (o.id == outputId) return o.firstChannel - 1 + (o.stereo ? 2 : 1) <= engine::kMaxDeviceChannels;
    return false;
}

float clipOverDb(const OutputPeak& peak) { return std::max(0.1f, std::round(peak.peakDb * 10.0f) / 10.0f); }

juce::String describeClip(const OutputPeak& peak, const juce::String& outputName, const juce::String& songTitle) {
    return outputName + " clips in " + songTitle + " at bar " + juce::String(peak.bar) + ", by " + juce::String(clipOverDb(peak), 1) + " dB.";
}

juce::String describeLimit(const OutputPeak& peak, const juce::String& outputName, const juce::String& songTitle) {
    return outputName + "'s Limit takes " + juce::String(peak.limitDb, 1) + " dB in " + songTitle + " at bar "
           + juce::String(peak.limitBar) + ".";
}

}  // namespace sqlive::model
