#pragma once

#include <cmath>

namespace sqlive::model {

inline constexpr float kMinDb = -60.0f, kMaxDb = 6.0f;

// At or below kMinDb the gain is exactly zero (silence), not a very small number.
inline float dbToGain(float db) noexcept { return db <= kMinDb ? 0.0f : std::pow(10.0f, db / 20.0f); }

// At or below 0.001 (-60 dB), and for anything that isn't a positive number, the answer is kMinDb.
inline float gainToDb(float gain) noexcept { return gain > 0.001f ? 20.0f * std::log10(gain) : kMinDb; }

// Into [kMinDb, kMaxDb]; NaN becomes 0 dB.
inline float clampDb(float db) noexcept {
    if (std::isnan(db)) return 0.0f;
    return db < kMinDb ? kMinDb : (db > kMaxDb ? kMaxDb : db);
}

// A pan: -100 is hard left, 0 the centre, +100 hard right. Into that range.
inline constexpr int kMinPan = -100, kMaxPan = 100;
inline int clampPan(int pan) noexcept { return pan < kMinPan ? kMinPan : (pan > kMaxPan ? kMaxPan : pan); }

}  // namespace sqlive::model
