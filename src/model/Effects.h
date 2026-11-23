#pragma once

#include "model/Documents.h"

namespace sqlive::model {

// A chain of effects on a part (the song's, for every output) or on an output (the whole mix of it).
inline constexpr int kMaxEffects = 4;

enum class EffectPlace { Part, Output };

// The kinds a chain can be given, in the order of the add menu. Width only for stereo (a stereo part, a stereo output).
//   Part: eq, dynamic, deess, width, transient.   Output: eq, dynamic, limit, width.
juce::StringArray effectKindsFor(EffectPlace, bool stereo);

// "EQ", "Dynamic", "De-Ess", "Width", "Transient", "Limit"; a kind not known gives the kind itself.
juce::String effectName(const juce::String& kind);

// What an effect of that kind starts as: Smart Q's defaults (a Dynamic changes nothing below 0 dBFS until it is set). An EQ has
// no band in use ("bands" is an empty list). A kind not known gives an effect with no settings.
EffectDoc defaultEffect(const juce::String& kind);

// The undo words of a step on an effect: "<verb> <effect> on <where>", or "<effect> on <where>" with no verb, as for a setting:
// effectWords("add", "eq", "Keys") is "add EQ on Keys"; effectWords("switch off", "eq", "Keys") is "switch off EQ on Keys";
// effectWords({}, "eq", "Keys") is "EQ on Keys". where is the part's or the output's name.
juce::String effectWords(const juce::String& verb, const juce::String& kind, const juce::String& where);

}  // namespace sqlive::model
