#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "engine/Engine.h"
#include "engine/MixSetup.h"
#include "engine/PreparedSong.h"
#include "engine/TempoMap.h"
#include "engine/Types.h"

// Songs, mixes and engine helpers the engine tests share.
namespace sqlive::test {

// `frames` samples of `left`, and of `right` unless it is NaN, which makes the audio mono.
inline std::shared_ptr<const engine::PartAudio> constantAudio(engine::SamplePos frames, float left, float right) {
    auto audio = std::make_shared<engine::PartAudio>();
    audio->left.assign(static_cast<std::size_t>(frames), left);
    if (!std::isnan(right)) audio->right.assign(static_cast<std::size_t>(frames), right);
    return audio;
}

// One mono part of `value` on role 0, 120 BPM, 48 kHz.
inline engine::PreparedSong testSong(std::string id, engine::SamplePos frames, float value, int countInBars = 0, bool click = false) {
    return engine::prepareSong(std::move(id), engine::TempoMap{}, countInBars, 0, {{constantAudio(frames, value, NAN), 0}},
                               engine::ClickSetup{.enabled = click}, 48000.0);
}

// One mono part whose sample i has the value float(i), on role 0, 120 BPM 4/4 at 48 kHz (a bar is 96000 samples), `bars`
// bars long, with no click and no count-in; sections of the lengths given (in bars, from bar 1), then the arrangement,
// then finishSections.
inline engine::PreparedSong rampSong(std::string id, int bars, std::vector<int> barsPerSection,
                                     std::vector<std::uint16_t> arrangement = {}) {
    constexpr engine::SamplePos kBar = 96000;
    auto audio = std::make_shared<engine::PartAudio>();
    audio->left.resize(static_cast<std::size_t>(bars * kBar));
    for (std::size_t i = 0; i < audio->left.size(); ++i) audio->left[i] = static_cast<float>(i);
    auto song = engine::prepareSong(std::move(id), engine::TempoMap{}, 0, 0, {{audio, 0}},
                                    engine::ClickSetup{.enabled = false}, 48000.0);
    song.sections.clear();
    engine::SamplePos at = 0;
    for (const int n : barsPerSection) {
        song.sections.push_back({at, at + n * kBar});
        at += n * kBar;
    }
    song.arrangement = std::move(arrangement);
    engine::finishSections(song);
    return song;
}

// Two roles, role 1 the click, one stereo bus on channels 0 and 1, every gain 1.
inline std::shared_ptr<const engine::MixSetup> stereoMix() {
    return std::make_shared<const engine::MixSetup>(
        engine::MixSetup{.buses = {{0, true, 1.0f}}, .roleCount = 2, .clickRole = 1, .gain = {1.0f, 1.0f}});
}

// Two roles, role 1 the click, two mono buses: the parts on channel 0 and the click on channel 1, every gain 1.
inline std::shared_ptr<const engine::MixSetup> splitMix() {
    return std::make_shared<const engine::MixSetup>(engine::MixSetup{
        .buses = {{0, false, 1.0f}, {1, false, 1.0f}}, .roleCount = 2, .clickRole = 1, .gain = {1.0f, 0.0f, 0.0f, 1.0f}});
}

inline std::shared_ptr<const engine::PreparedSong> share(engine::PreparedSong song) {
    return std::make_shared<const engine::PreparedSong>(std::move(song));
}

// Pops the next event that is not a StepChanged (passing over those): for tests of a song's start, end and stop.
inline bool popSongEvent(engine::Engine& e, engine::EngineEvent& ev) {
    while (e.popEvent(ev))
        if (ev.type != engine::EngineEvent::Type::StepChanged) return true;
    return false;
}

// Pops every event the engine has reported so far and returns their types, oldest first, StepChanged included.
inline std::vector<engine::EngineEvent::Type> drainAllEvents(engine::Engine& e) {
    std::vector<engine::EngineEvent::Type> types;
    engine::EngineEvent ev;
    while (e.popEvent(ev)) types.push_back(ev.type);
    return types;
}

// The same, leaving out StepChanged: the song's start, end and stop.
inline std::vector<engine::EngineEvent::Type> drainEvents(engine::Engine& e) {
    std::vector<engine::EngineEvent::Type> types;
    engine::EngineEvent ev;
    while (popSongEvent(e, ev)) types.push_back(ev.type);
    return types;
}

} // namespace sqlive::test
