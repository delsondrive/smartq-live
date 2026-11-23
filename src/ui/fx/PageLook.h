#pragma once

namespace smartq {

// What the pages, the cards and the graphs agree on, so that each is said once.

// The rate a picture is drawn for while the host has not said one.
inline constexpr double kFallbackSampleRate = 48000.0;

// How much of its colour a bypassed module's page keeps.
inline constexpr float kBypassedOpacity = 0.4f;

// How often the reading of how far a module is turning the track down is read, on a page and on a card.
inline constexpr double kReadingSeconds = 0.1;

}  // namespace smartq
