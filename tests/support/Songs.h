#pragma once

#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "engine/MixSetup.h"
#include "engine/PreparedSong.h"
#include "engine/TempoMap.h"
#include "engine/Types.h"

// Songs and mixes the engine tests share.
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

// Two roles, role 1 the click, one stereo bus on channels 0 and 1, every gain 1.
inline std::shared_ptr<const engine::MixSetup> stereoMix() {
    return std::make_shared<const engine::MixSetup>(
        engine::MixSetup{.buses = {{0, true, 1.0f}}, .roleCount = 2, .clickRole = 1, .gain = {1.0f, 1.0f}});
}

inline std::shared_ptr<const engine::PreparedSong> share(engine::PreparedSong song) {
    return std::make_shared<const engine::PreparedSong>(std::move(song));
}

} // namespace sqlive::test
