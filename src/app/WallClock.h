#pragma once

#include <juce_core/juce_core.h>

namespace sqlive::app {

// The one clock the app times things by, in seconds: the audio block's anchor and the MIDI dispatcher, held keys and
// pedals, the tap tempo. juce::Time::getMillisecondCounterHiRes: it never jumps with the wall clock's time of day.
inline double wallClockSeconds() noexcept { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

}  // namespace sqlive::app
