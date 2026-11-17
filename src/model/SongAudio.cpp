#include "model/SongAudio.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

#include <juce_audio_formats/juce_audio_formats.h>

#include "engine/MidiClock.h"
#include "model/Resampler.h"
#include "model/Routing.h"
#include "model/Sections.h"
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
    return 0;   // "wood", and a clip of the band's own while it isn't loaded
}

bool isClipSound(const juce::String& name) { return name != "wood" && name != "beep" && name != "rim"; }

// A jump's crossfade as the engine makes it (Engine.cpp's kJumpFadeAt48k): 240 samples at 48 kHz, scaled to the rate.
std::int64_t jumpFadeLength(double sampleRate) { return std::llround(240.0 * sampleRate / 48000.0); }

std::shared_ptr<const engine::PartAudio> clipAt(const PrepareOptions& options, const juce::String& path) {
    if (path.isEmpty() || !options.clips) return nullptr;
    return options.clips(path);
}

std::vector<engine::AutoPoint> automationInSamples(const std::vector<AutomationPoint>& points, const engine::TempoMap& tempo,
                                                   double sampleRate) {
    std::vector<engine::AutoPoint> out;
    out.reserve(points.size());
    for (const auto& p : points) out.push_back({tempo.sampleAt({p.bar, p.beat}, sampleRate), p.db});
    // In time order, as the engine assumes; a tie keeps its written order (a mute is two points at the same time).
    std::stable_sort(out.begin(), out.end(), [](const engine::AutoPoint& a, const engine::AutoPoint& b) { return a.at < b.at; });
    return out;
}

// The song's sections, arrangement and section cues, from its documents. Left as prepareSong made them (one section for
// the whole song) when the song has no sections, or none starts before the audio ends.
void prepareSections(engine::PreparedSong& prepared, const SongDoc& song, const BandDoc& band, const PrepareOptions& options) {
    if (song.sections.empty()) return;
    const auto rate = prepared.sampleRate;
    const auto bar = [&](int number) { return prepared.tempo.sampleAt({number, 1.0}, rate); };
    auto tiled = tileSections(song, barsInSong(song));
    // Those starting where the audio has ended would play nothing. The first starts at bar 1, so it goes only when there
    // is no audio at all.
    std::erase_if(tiled, [&](const TiledSection& t) { return bar(t.startBar) >= prepared.end; });
    if (tiled.empty()) return;
    // The last one left ends where the audio does; its cue is placed from the first bar line at or after that.
    auto& last = tiled.back();
    last.endBar = last.startBar + 1;
    while (bar(last.endBar) < prepared.end) ++last.endBar;
    auto steps = resolveArrangement(song, options.arrangementId, tiled);
    // A last section shorter than a jump's crossfade would be cut short by a hand-over still fading into it: it joins the
    // section before it, and the steps that named it play that one.
    const int lastIndex = (int) tiled.size() - 1;
    if (lastIndex > 0 && prepared.end - bar(tiled.back().startBar) < jumpFadeLength(rate)) {
        tiled[(std::size_t) lastIndex - 1].endBar = tiled.back().endBar;
        tiled.pop_back();
        for (auto& step : steps)
            if (step == lastIndex) step = lastIndex - 1;
    }

    prepared.sections.clear();
    prepared.arrangement.clear();
    prepared.sectionCue.clear();
    prepared.announceAt.clear();
    for (std::size_t i = 0; i < tiled.size() && i <= 0xFFFF; ++i) {
        const auto& t = tiled[i];
        prepared.sections.push_back({bar(t.startBar), i + 1 == tiled.size() ? prepared.end : bar(t.endBar)});
        const auto type = std::find_if(band.sectionTypes.begin(), band.sectionTypes.end(),
                                       [&](const SectionTypeDoc& s) { return t.typeId.isNotEmpty() && s.id == t.typeId; });
        prepared.sectionCue.push_back(type == band.sectionTypes.end() ? nullptr : clipAt(options, type->cueClip));
        prepared.announceAt.push_back(prepared.tempo.sampleAt(announcePlace(song, t, band.cueLead), rate));
    }
    for (const int step : steps)
        if (step >= 0 && (std::size_t) step < prepared.sections.size()) prepared.arrangement.push_back((std::uint16_t) step);
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
                                       double sampleRate, const PrepareOptions& options) {
    const auto tempo = tempoMapOf(song);
    std::vector<engine::PreparedPart> parts;
    for (const auto& part : song.parts) {
        const auto found = audioByPartId.find(part.id);
        const int role = roleIndex(band, part.roleId);
        if (found == audioByPartId.end() || found->second == nullptr || role < 0) continue;
        parts.push_back({found->second, role, dbToGain(part.gainDb), part.mute, part.id.toStdString()});
        if (const auto points = song.automation.find(part.id); points != song.automation.end())
            parts.back().automation = automationInSamples(points->second, tempo, sampleRate);
    }
    engine::ClickSetup click;
    click.enabled = song.click.enabled;
    click.gain = dbToGain(band.clickDb + song.click.offsetDb);
    click.subdivision = subdivisionNamed(song.click.subdivision);
    click.sound = clickSoundNamed(song.click.sound);
    click.accentDownbeat = song.click.accentDownbeat;
    auto prepared = engine::prepareSong(song.id.toStdString(), tempo, song.countInBars,
                                        (engine::SamplePos) std::llround(song.audioStartSeconds * sampleRate), std::move(parts),
                                        click, sampleRate);
    if (isClipSound(song.click.sound))
        if (const auto clip = clipAt(options, song.click.sound); clip != nullptr && !clip->left.empty())
            prepared.clickSound = engine::clickSoundFromClip(clip->left, sampleRate);
    if (const auto points = song.automation.find("click"); points != song.automation.end())
        prepared.clickAutomation = automationInSamples(points->second, tempo, sampleRate);
    if (const auto points = song.automation.find("cues"); points != song.automation.end())
        prepared.cuesAutomation = automationInSamples(points->second, tempo, sampleRate);

    prepared.jumpTiming = song.jumpTiming == JumpTiming::NextBar ? engine::JumpTiming::NextBar : engine::JumpTiming::EndOfSection;
    prepared.cueGain = dbToGain(band.cuesDb);
    for (const auto& cue : song.cues)
        if (auto clip = clipAt(options, cue.clip))
            prepared.cues.push_back({tempo.sampleAt({cue.bar, cue.beat}, sampleRate), std::move(clip)});
    prepareSections(prepared, song, band, options);
    engine::finishSections(prepared);
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
        prepared.clockTicks = engine::buildClockTicks(
            prepared.tempo, sampleRate, engine::countOffFloor(prepared.tempo, sampleRate, prepared.start), prepared.end);
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
