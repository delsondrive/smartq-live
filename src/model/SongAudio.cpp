#include "model/SongAudio.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

#include <juce_audio_formats/juce_audio_formats.h>

#include "engine/MidiClock.h"
#include "model/Resampler.h"
#include "model/Routing.h"
#include "model/SongMidi.h"
#include "model/Units.h"

namespace sqlive::model {

namespace {

engine::Subdivision subdivisionNamed(const juce::String& name) {
    if (name == "eighths") return engine::Subdivision::Eighths;
    if (name == "triplets") return engine::Subdivision::Triplets;
    if (name == "sixteenths") return engine::Subdivision::Sixteenths;
    return engine::Subdivision::None;
}

int clickSoundNamed(const juce::String& name) {
    if (name == "beep") return 1;
    if (name == "rim") return 2;
    return 0;   // "wood"
}

}  // namespace

std::shared_ptr<const engine::PartAudio> loadPartAudio(const juce::File& wav, double targetRate,
                                                       const std::function<bool(double progress)>& keepGoing) {
    if (targetRate <= 0.0) return nullptr;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(wav));
    if (reader == nullptr || reader->numChannels == 0 || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0) return nullptr;

    const int channels = std::min(2, (int) reader->numChannels);
    const auto expected = (size_t) std::llround((double) reader->lengthInSamples * targetRate / reader->sampleRate);
    try {
        auto audio = std::make_shared<engine::PartAudio>();
        audio->left.reserve(expected);
        if (channels == 2) audio->right.reserve(expected);
        const auto end = readAtRate(*reader, channels, targetRate, keepGoing, [&](const juce::AudioBuffer<float>& block, int frames) {
            const float* l = block.getReadPointer(0);
            audio->left.insert(audio->left.end(), l, l + frames);
            if (channels == 2) {
                const float* r = block.getReadPointer(1);
                audio->right.insert(audio->right.end(), r, r + frames);
            }
            return true;
        });
        if (end != ReadEnd::Done || audio->left.empty()) return nullptr;
        return audio;
    } catch (const std::bad_alloc&) {
        return nullptr;   // too long to hold in memory
    }
}

engine::PreparedSong buildPreparedSong(const SongDoc& song, const BandDoc& band, const RigDoc& rig,
                                       const std::map<juce::String, std::shared_ptr<const engine::PartAudio>>& audioByPartId,
                                       double sampleRate) {
    std::vector<engine::PreparedPart> parts;
    for (const auto& part : song.parts) {
        const auto found = audioByPartId.find(part.id);
        const int role = roleIndex(band, part.roleId);
        if (found == audioByPartId.end() || found->second == nullptr || role < 0) continue;
        parts.push_back({found->second, role, dbToGain(part.gainDb), part.mute, part.id.toStdString()});
    }
    engine::ClickSetup click;
    click.enabled = song.click.enabled;
    click.gain = dbToGain(band.clickDb + song.click.offsetDb);
    click.subdivision = subdivisionNamed(song.click.subdivision);
    click.sound = clickSoundNamed(song.click.sound);
    click.accentDownbeat = song.click.accentDownbeat;
    auto prepared = engine::prepareSong(song.id.toStdString(), tempoMapOf(song), song.countInBars,
                                        (engine::SamplePos) std::llround(song.audioStartSeconds * sampleRate), std::move(parts),
                                        click, sampleRate);
    auto midi = buildMidi(song, band, rig, prepared.tempo, sampleRate);
    // An event a device's offset puts before the song's first sample (a note at bar 1, early, with no count-in or lead-in)
    // would never play: it is sent at that first sample instead. Moving every such event keeps them in order, so a short
    // note's note-off still follows its note-on; a program or control change there is covered by the chase all the same.
    for (auto& event : midi.events) event.at = std::max(event.at, prepared.start);
    prepared.midi = std::move(midi.events);
    prepared.clockDevices = std::move(midi.clockDevices);
    prepared.startStopDevices = std::move(midi.startStopDevices);
    prepared.midiDeviceIds = std::move(midi.deviceIds);
    if (!prepared.clockDevices.empty())
        prepared.clockTicks = engine::buildClockTicks(prepared.tempo, sampleRate, prepared.start, prepared.end);
    return prepared;
}

engine::TempoMap tempoMapOf(const SongDoc& song) {
    std::vector<engine::TempoPoint> points;
    for (const auto& p : song.tempo) points.push_back({p.bar, p.beat, p.bpm, p.numerator, p.denominator});
    return engine::TempoMap(std::move(points));
}

std::pair<double, double> songExtentSeconds(const SongDoc& song, const engine::TempoMap& tempo) {
    const double countIn = -tempo.secondsAt({1 - std::max(0, song.countInBars), 1.0});
    const double start = -std::max(countIn, song.audioStartSeconds);
    const double end = std::max(song.lengthSeconds - song.audioStartSeconds, tempo.secondsAt({2, 1.0}));
    return {start, std::max(end, start + 1.0)};
}

std::int64_t bytesInMemory(const SongDoc& song, double sampleRate) {
    const auto frames = (std::int64_t) std::llround(song.lengthSeconds * sampleRate);
    std::int64_t bytes = 0;
    for (const auto& part : song.parts)   // loadPartAudio keeps at most two channels
        bytes += frames * std::clamp(part.channels, 0, 2) * (std::int64_t) sizeof(float);
    return bytes;
}

}  // namespace sqlive::model
