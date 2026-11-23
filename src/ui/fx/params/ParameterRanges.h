#pragma once

#include <array>

// Copied from Smart Q (smarteq_vst src/plugin/ParameterRanges.h) as it is.

namespace smartq {

// The eight slope choices of the cuts, in dB per octave, in the order of the "slope" choice parameter.
// Shelves use only 6 or 12.
inline constexpr std::array<int, 8> kSlopeChoices{6, 12, 18, 24, 36, 48, 72, 96};

namespace ranges {

// The limits of the band controls. Parameters.cpp builds the host parameters from them and the AI
// command layer validates requests against them, so the two cannot drift apart.
inline constexpr double kMinFrequencyHz = 10.0, kMaxFrequencyHz = 30000.0;
inline constexpr double kMinQ = 0.025, kMaxQ = 40.0;
inline constexpr double kMaxGainDb = 30.0;  // gain and dynamic range are both -30 to +30
inline constexpr double kMaxOutputGainDb = 30.0;  // the plugin's and each module's output gain run from minus this to plus this
inline constexpr double kMinThresholdDb = -80.0, kMaxThresholdDb = 0.0;
inline constexpr double kMinAttackMs = 0.5, kMaxAttackMs = 200.0;
inline constexpr double kMinReleaseMs = 5.0, kMaxReleaseMs = 2000.0;

// The two slope choices of the shelves, in dB per octave.
inline constexpr int kShelfSlopeLow = 6, kShelfSlopeHigh = 12;

// The AI-facing limits (spec section 5). The command layer enforces them and the bridge's tool descriptions
// quote them, so they live here for both.
inline constexpr double kDefaultBoostLimitDb = 12.0, kMaxBoostLimitDb = 30.0;  // the limit is set per copy from 0 to the maximum
inline constexpr int kMinNoteLength = 1, kMaxNoteLength = 200;
inline constexpr int kMaxChangeEntries = 256;  // entries in one apply_changes batch
inline constexpr int kHistoryDepth = 20;       // AI batches a copy keeps for undo
inline constexpr int kAutomationCheckMs = 200;  // how long after a batch the layer checks for automation overriding it

}  // namespace ranges
}  // namespace smartq
