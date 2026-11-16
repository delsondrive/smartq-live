#include "model/DemoBand.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "engine/TempoMap.h"
#include "model/BandFolder.h"
#include "model/CoreCommands.h"
#include "model/DocumentsJson.h"

namespace sqlive::model {

namespace {

constexpr double kRate = 48000.0;
constexpr float kPeak = 0.15f;   // every part peaks here, below -16 dB (0.158), so three parts and the click stay below 0 dBFS

struct DemoSong {
    const char* title;
    double bpm;
    int numerator, denominator, bars;
    std::array<std::array<int, 3>, 4> chords;   // MIDI notes, one chord a bar, round and round
    std::array<int, 4> bass;                    // the bass note under each chord
};

const DemoSong kSongs[] = {
    {"First Light", 100.0, 4, 4, 16, {{{60, 64, 67}, {59, 62, 67}, {57, 60, 64}, {57, 60, 65}}}, {36, 43, 45, 41}},
    {"Low Tide", 72.0, 6, 8, 12, {{{57, 60, 64}, {57, 60, 65}, {55, 60, 64}, {55, 59, 62}}}, {45, 41, 36, 43}},
    {"Run", 140.0, 4, 4, 24, {{{56, 59, 64}, {54, 59, 63}, {56, 61, 64}, {57, 61, 64}}}, {40, 47, 37, 45}},
};

double hz(int midiNote) { return 440.0 * std::pow(2.0, (midiNote - 69) / 12.0); }

// 0..1..0 over `length` frames: up over the first `fade`, down over the last `fade`.
float fadeAt(std::int64_t i, std::int64_t length, std::int64_t fade) {
    const auto edge = std::min(i, length - 1 - i);
    return edge >= fade ? 1.0f : (float) edge / (float) fade;
}

struct Generated { std::vector<float> keys, bass, perc; };

Generated generate(const DemoSong& s) {
    const engine::TempoMap tempo(std::vector<engine::TempoPoint>{{1, 1.0, s.bpm, s.numerator, s.denominator}});
    const auto barStart = [&](int bar) { return tempo.sampleAt({bar, 1.0}, kRate); };
    const auto frames = (size_t) barStart(s.bars + 1);
    Generated g{std::vector<float>(frames), std::vector<float>(frames), std::vector<float>(frames)};
    const auto fade = (std::int64_t) std::llround(0.010 * kRate);
    const auto twoPi = juce::MathConstants<double>::twoPi;

    for (int bar = 1; bar <= s.bars; ++bar) {
        const auto from = barStart(bar), length = barStart(bar + 1) - from;
        const auto& chord = s.chords[(size_t) (bar - 1) % s.chords.size()];
        const double bassHz = hz(s.bass[(size_t) (bar - 1) % s.bass.size()]);
        for (std::int64_t i = 0; i < length; ++i) {
            const double t = (double) i / kRate;
            const float env = fadeAt(i, length, fade);
            double chordSum = 0.0;
            for (int note : chord) chordSum += std::sin(twoPi * hz(note) * t);
            g.keys[(size_t) (from + i)] = (float) (chordSum / 3.0) * kPeak * env;
            g.bass[(size_t) (from + i)] = (float) std::sin(twoPi * bassHz * t) * kPeak * env;
        }
    }

    // A 20 ms burst of noise on every beat, dying away, from a generator with a fixed seed.
    std::uint32_t state = 0x5eed1234u;
    const auto noise = [&state] {   // xorshift32, -1..1
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return (float) ((double) state / 2147483647.5 - 1.0);
    };
    const auto burst = (std::int64_t) std::llround(0.020 * kRate);
    for (int bar = 1; bar <= s.bars; ++bar)
        for (int beat = 1; beat <= s.numerator; ++beat) {
            const auto from = tempo.sampleAt({bar, (double) beat}, kRate);
            for (std::int64_t i = 0; i < burst && from + i < (std::int64_t) frames; ++i)
                g.perc[(size_t) (from + i)] = noise() * kPeak * (1.0f - (float) i / (float) burst);
        }
    return g;
}

juce::Result writeWav(const juce::File& file, const std::vector<float>& samples) {
    const auto couldnt = juce::Result::fail("Couldn't write " + file.getFullPathName() + ".");
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr) return couldnt;
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                  .withSampleRate(kRate)
                                                  .withNumChannels(1)
                                                  .withBitsPerSample(24));
    if (writer == nullptr) return couldnt;
    const float* channels[] = {samples.data()};
    if (! writer->writeFromFloatArrays(channels, 1, (int) samples.size())) return couldnt;
    writer.reset();   // writes the header and closes the file
    return juce::Result::ok();
}

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> items) {
    auto* o = new juce::DynamicObject();
    for (const auto& [key, value] : items) o->setProperty(juce::Identifier(key), value);
    return juce::var(o);
}

juce::Result makeDemo(const juce::File& root) {
    if (auto r = BandFolder::create(root, "Demo band"); r.failed()) return r;
    BandFolder folder(root);
    BandModel model;
    folder.load(model);
    if (model.setlistIds().empty()) return juce::Result::fail("The new band folder has no setlist.");
    CommandLayer commands(model);
    registerCoreCommands(commands);
    std::set<DocKey> changed;
    commands.onChanged = [&changed](const std::vector<DocKey>& keys) { changed.insert(keys.begin(), keys.end()); };
    const auto run = [&commands](const char* name, const juce::var& params) {
        auto done = commands.run(name, params);
        return done.ok ? juce::Result::ok() : juce::Result::fail(done.message);
    };
    const auto roleNamed = [&model](const char* name) {
        for (const auto& r : model.band().roles)
            if (r.name == name) return r.id;
        return juce::String();
    };

    const auto setlistId = model.setlistIds()[0];
    if (auto r = run("setlist.set", object({{"setlistId", setlistId}, {"name", "Demo set"}})); r.failed()) return r;

    for (const auto& spec : kSongs) {
        SongDoc song;
        song.id = newId();
        song.title = spec.title;
        song.countInBars = 1;
        song.tempo = {TempoPointDoc{1, 1.0, spec.bpm, spec.numerator, spec.denominator, {}}};

        const auto songDir = folder.assignSongFolder(song.id, song.title);
        if (! songDir.isDirectory()) return juce::Result::fail("Couldn't make a folder for " + song.title + ".");
        const auto audioDir = songDir.getChildFile("audio");
        if (! audioDir.createDirectory()) return juce::Result::fail("Couldn't make the folder " + audioDir.getFullPathName() + ".");
        const auto audio = generate(spec);
        const std::pair<const char*, const std::vector<float>*> parts[] = {
            {"Keys", &audio.keys}, {"Bass", &audio.bass}, {"Perc", &audio.perc}};
        for (const auto& [name, samples] : parts) {
            const juce::String file = juce::String("audio/") + name + ".wav";
            if (auto r = writeWav(audioDir.getChildFile(juce::String(name) + ".wav"), *samples); r.failed()) return r;
            song.parts.push_back({newId(), name, file, roleNamed(name), 0.0f, false, 1, {}});
        }
        song.lengthSeconds = (double) audio.keys.size() / kRate;

        if (auto r = run("song.add", object({{"song", toVar(song)}})); r.failed()) return r;
        if (auto r = run("setlist.addSong", object({{"setlistId", setlistId}, {"songId", song.id}})); r.failed()) return r;
    }

    for (const auto& key : changed)
        if (auto r = folder.save(key, model); r.failed()) return r;
    return juce::Result::ok();
}

}  // namespace

juce::Result createDemoBand(const juce::File& root) {
    const bool existed = root.exists();
    auto result = makeDemo(root);
    if (result.failed() && ! existed) root.deleteRecursively();
    return result;
}

}  // namespace sqlive::model
