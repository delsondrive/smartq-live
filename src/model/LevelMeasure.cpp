#include "model/LevelMeasure.h"

#include <algorithm>
#include <cmath>

#include "engine/Engine.h"
#include "model/Routing.h"

namespace sqlive::model {

namespace {

constexpr int kBlock = 4096;
constexpr int kBlocksBetweenAsks = 64;

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
    float peak = 0.0f;
    engine::SamplePos at = 0;             // the song time of its peak
};

}  // namespace

SongLevels measureSong(std::shared_ptr<const engine::PreparedSong> song, const BandDoc& band, const RigDoc& rig,
                       double sampleRate, const std::function<bool(double progress)>& keepGoing) {
    SongLevels levels;
    if (song == nullptr) return levels;
    levels.songId = juce::String(song->id);

    // Every output, in the rig's order, with the device channels its bus is on (as the mix builds it, all on the device).
    std::vector<Watch> watches;
    int deviceChannels = 1;
    for (const auto& o : rig.outputs) {
        Watch w;
        if (busIndexOf(rig, o.id, 0) >= 0) {
            w.firstChannel = o.firstChannel - 1;
            w.channels = o.stereo ? 2 : 1;
            deviceChannels = std::max(deviceChannels, w.firstChannel + w.channels);
        }
        watches.push_back(w);
    }
    deviceChannels = std::min(deviceChannels, engine::kMaxDeviceChannels);

    auto engine = std::make_unique<engine::Engine>();
    engine->prepare(sampleRate, kBlock);
    engine->setMix(buildMixSetup(band, rig, 0));
    engine->cue(song);
    engine->start();

    std::vector<std::vector<float>> buffers(static_cast<std::size_t>(deviceChannels), std::vector<float>(kBlock, 0.0f));
    std::vector<float*> out;
    for (auto& b : buffers) out.push_back(b.data());

    const engine::SamplePos expected = arrangementLength(*song);
    const engine::SamplePos most = expected + static_cast<engine::SamplePos>(std::llround(2.0 * sampleRate));
    engine::SamplePos played = 0;
    engine::SamplePos songTime = song->start;   // where the next block begins in the song
    int step = -1;
    bool stopped = false;
    const bool playable = song->sampleRate == sampleRate;   // the engine plays nothing made at another rate
    for (int block = 0; playable && played < most; ++block) {
        if (block > 0 && block % kBlocksBetweenAsks == 0
            && !keepGoing(expected > 0 ? std::min(1.0, static_cast<double>(played) / static_cast<double>(expected)) : 1.0))
            break;
        engine::SamplePos frames = std::min<engine::SamplePos>(kBlock, most - played);
        if (const auto handOver = nextHandOver(*song, step, songTime); handOver > songTime)
            frames = std::min(frames, handOver - songTime);
        const int n = static_cast<int>(frames);
        engine->process(out.data(), deviceChannels, n);
        played += n;

        // A block runs straight through the song unless it began with a jump (taken at its first sample). One the song
        // ended in reports no song time: it ran straight on to the end.
        const auto status = engine->status();
        const bool straight = status.state == engine::PlayState::Stopped || status.songTime - n == songTime;
        const engine::SamplePos first = straight ? songTime : status.songTime - n;
        for (auto& w : watches)
            for (int c = w.firstChannel; c < w.firstChannel + w.channels; ++c) {
                const float* samples = buffers[static_cast<std::size_t>(c)].data();
                for (int i = 0; i < n; ++i)
                    if (const float m = std::abs(samples[i]); m > w.peak) {
                        w.peak = m;
                        w.at = first + i;
                    }
            }
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

    levels.complete = stopped;
    for (std::size_t i = 0; i < watches.size(); ++i) {
        const auto& w = watches[i];
        OutputPeak peak;
        peak.outputId = rig.outputs[i].id;
        if (w.peak > 0.0f) {
            peak.peakDb = std::max(-100.0f, 20.0f * std::log10(w.peak));
            peak.clips = w.peak > 1.0f;
            if (w.at > 0)
                peak.bar = std::max(1, song->tempo.barBeatAt(static_cast<double>(w.at) / song->sampleRate).bar);
        }
        levels.outputs.push_back(peak);
    }
    return levels;
}

juce::String describeClip(const OutputPeak& peak, const juce::String& outputName, const juce::String& songTitle) {
    const float over = std::max(0.1f, std::round(peak.peakDb * 10.0f) / 10.0f);
    return outputName + " clips in " + songTitle + " at bar " + juce::String(peak.bar) + ", by " + juce::String(over, 1) + " dB.";
}

}  // namespace sqlive::model
