#pragma once

#include <vector>

#include "engine/EffectChain.h"
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

// What the engine plays for an effect. Every value is held to Smart Q's range for it (ModuleControls.h, ParameterRanges.h), as
// a file's values are only type-checked; a value missing, or not a number or a switch, is the kind's default. A Choice is its
// index, rounded and held to the choices; a band's slope is one of Smart Q's eight (6 to 96 dB per octave) by its index, and at
// most the first 24 bands are used. on false is bypassed. A Dynamic plays no lookahead, on a part or an output: Smart Q Live's
// effects add no delay but a Limit's. A kind not known gives an EQ that does nothing (chainOf leaves it out).
engine::ModuleSettings settingsOf(const EffectDoc&, EffectPlace);

// The engine's chain for a part's or an output's effects, in order: each effect the place plays (effectKindsFor; so no Width
// unless stereo, and no kind not known), as settingsOf gives it.
std::vector<engine::ModuleSettings> chainOf(const std::vector<EffectDoc>&, EffectPlace, bool stereo);

// The reverse of settingsOf: the effect with the settings' kind and every value of them (a band for each enabled one), keeping
// its `on` and `extra` (the settings' bypass is not written: that is `on`).
EffectDoc withSettings(EffectDoc, const engine::ModuleSettings&);

}  // namespace sqlive::model
