#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <span>
#include <string_view>

#include "ui/fx/params/ParameterRanges.h"
#include "ui/fx/params/Slots.h"

// Copied from Smart Q (smarteq_vst src/plugin/ModuleControls.h) with the tables of the six kinds Smart Q Live plays; the others' are
// left out, and so are the AI's names of the choices. model/Effects.cpp holds Live's documents to the same ranges and defaults.

namespace smartq {

// What a module type offers besides any bands, one row to a control. The parameters, the AI's tools and the undo records
// are all driven from this table, so what a control is called, its range, its default, its choices and whether it raises
// the level are written here and nowhere else. What a control does is the module's own. A table's order is permanent: a
// control's place in it is its index in ChainControl, and the host's list is built in this order.
enum class ControlKind { Float, Bool, Choice };

// A Float control's centre for a range whose steps are equal ratios: the knob's middle is the range's geometric mean.
inline constexpr double kEqualRatios = -1.0;

// How a control that raises the level says by how much. Decibels: its value is the dB it raises by. WidthPercent: a value above 100
// raises the side by 20 log10(value / 100) dB, and one at or below it raises nothing.
enum class BoostLaw { Decibels, WidthPercent };

struct ControlSpec {
    const char* field;      // the parameter ID's end: moduleParamId(slot, field)
    const char* jsonName;   // the AI's name for it: "output_gain_db"
    const char* hostName;   // after the slot's name in the host's list: "Output"
    ControlKind kind;
    double minimum, maximum, defaultValue;
    double centre;          // a Float's law: 0 is linear, kEqualRatios is equal ratios, and a value inside the range is the one at the knob's middle
    bool raisesLevel;       // the boost limit applies
    const char* unit;       // "dB", "ms", "%", or ""
    std::span<const char* const> choices;  // for Choice, in permanent order
    bool aiRaisesOnlyToHandSet = false;  // the AI may lower it, and raise it no higher than the value last set outside the AI
    BoostLaw boostLaw = BoostLaw::Decibels;  // for one that raisesLevel
    // For one that raisesLevel: the field of a Bool control of the same table that, while it is on, keeps this one from raising the level,
    // so that the boost limit doesn't apply to it then. Null for one that always raises it.
    const char* boostOnlyWhenOff = nullptr;
};

std::span<const ControlSpec> controlsOf(ModuleType);

// The tables themselves, where a compile-time check can read them. Defaults are neutral: a Dynamic that is added changes nothing below
// 0 dBFS until it is set.
inline constexpr std::array<const char*, 2> kDetectionLabels{"Peak", "RMS"};
inline constexpr std::array<const char*, 2> kListenLabels{"This track", "Sidechain"};
inline constexpr std::array<const char*, 5> kChannelLabels{"Stereo", "Left", "Right", "Mid", "Side"};

inline constexpr std::array<ControlSpec, 2> kEqControls{{
    {"output_gain", "output_gain_db", "Output", ControlKind::Float, -ranges::kMaxOutputGainDb, ranges::kMaxOutputGainDb, 0.0, 0.0, true, "dB", {}},
    {"bypass", "bypassed", "Bypass", ControlKind::Bool, 0.0, 1.0, 0.0, 0.0, false, "", {}},
}};

inline constexpr std::array<ControlSpec, 15> kDynamicControls{{
    {"threshold", "threshold_db", "Threshold", ControlKind::Float, -60.0, 0.0, 0.0, 0.0, false, "dB", {}},
    {"ratio", "ratio", "Ratio", ControlKind::Float, 1.0, 20.0, 3.0, 2.0, false, "", {}},
    {"attack", "attack_ms", "Attack", ControlKind::Float, 0.1, 200.0, 12.0, kEqualRatios, false, "ms", {}},
    {"release", "release_ms", "Release", ControlKind::Float, 5.0, 2000.0, 120.0, kEqualRatios, false, "ms", {}},
    {"auto_release", "auto_release", "Auto Release", ControlKind::Bool, 0.0, 1.0, 0.0, 0.0, false, "", {}},
    {"knee", "knee_db", "Knee", ControlKind::Float, 0.0, 24.0, 0.0, 0.0, false, "dB", {}},
    {"makeup", "makeup_db", "Make-up", ControlKind::Float, -12.0, 24.0, 0.0, 0.0, true, "dB", {}},
    {"mix", "mix_percent", "Mix", ControlKind::Float, 0.0, 100.0, 100.0, 0.0, false, "%", {}},
    {"detection", "detection", "Detection", ControlKind::Choice, 0.0, 1.0, 0.0, 0.0, false, "", kDetectionLabels},
    {"rms_window", "rms_window_ms", "RMS Window", ControlKind::Float, 1.0, 300.0, 50.0, kEqualRatios, false, "ms", {}},
    {"listen", "listen", "Listens to", ControlKind::Choice, 0.0, 1.0, 0.0, 0.0, false, "", kListenLabels},
    {"sc_highpass", "sc_highpass_hz", "Sidechain high-pass", ControlKind::Float, 20.0, 500.0, 20.0, kEqualRatios, false, "Hz", {}},
    {"lookahead", "lookahead_ms", "Lookahead", ControlKind::Float, 0.0, 10.0, 0.0, 0.0, false, "ms", {}},
    {"channel", "channel", "Channel", ControlKind::Choice, 0.0, 4.0, 0.0, 0.0, false, "", kChannelLabels},
    {"bypass", "bypassed", "Bypass", ControlKind::Bool, 0.0, 1.0, 0.0, 0.0, false, "", {}},
}};

inline constexpr std::array<const char*, 2> kDeEssModeLabels{"Split", "Wide"};

inline constexpr std::array<ControlSpec, 5> kDeEssControls{{
    {"frequency", "frequency_hz", "Frequency", ControlKind::Float, 2000.0, 12000.0, 6000.0, kEqualRatios, false, "Hz", {}},
    {"threshold", "threshold_db", "Threshold", ControlKind::Float, -60.0, 0.0, 0.0, 0.0, false, "dB", {}},
    {"range", "range_db", "Range", ControlKind::Float, -24.0, 0.0, -6.0, 0.0, false, "dB", {}},
    {"mode", "mode", "Mode", ControlKind::Choice, 0.0, 1.0, 0.0, 0.0, false, "", kDeEssModeLabels},
    {"bypass", "bypassed", "Bypass", ControlKind::Bool, 0.0, 1.0, 0.0, 0.0, false, "", {}},
}};

inline constexpr std::array<const char*, 2> kLimitModeLabels{"Clean", "Multiband"};

inline constexpr std::array<ControlSpec, 12> kLimitControls{{
    {"input_gain", "input_gain_db", "Input Gain", ControlKind::Float, 0.0, 24.0, 0.0, 0.0, true, "dB", {}},
    {"ceiling", "ceiling_db", "Ceiling", ControlKind::Float, -24.0, 0.0, 0.0, 0.0, false, "dB", {}, true},
    {"release", "release_ms", "Release", ControlKind::Float, 1.0, 1000.0, 100.0, kEqualRatios, false, "ms", {}},
    {"auto_release", "auto_release", "Auto Release", ControlKind::Bool, 0.0, 1.0, 1.0, 0.0, false, "", {}},
    {"lookahead", "lookahead_ms", "Lookahead", ControlKind::Float, 1.0, 5.0, 1.5, 0.0, false, "ms", {}},
    {"true_peak", "true_peak", "True Peak", ControlKind::Bool, 0.0, 1.0, 0.0, 0.0, false, "", {}},
    {"bypass", "bypassed", "Bypass", ControlKind::Bool, 0.0, 1.0, 0.0, 0.0, false, "", {}},
    // Stage 5b's, after the rest so that no other control's place moves. Their parameters go at the very end of the host's list
    // (kControlsAddedLater in Parameters.cpp).
    {"soft_clip", "soft_clip_percent", "Soft Clip", ControlKind::Float, 0.0, 100.0, 0.0, 0.0, false, "%", {}},
    {"stereo_link", "stereo_link_percent", "Stereo Link", ControlKind::Float, 0.0, 100.0, 100.0, 0.0, false, "%", {}},
    {"punch", "punch_percent", "Punch", ControlKind::Float, 0.0, 100.0, 0.0, 0.0, false, "%", {}},
    {"upward", "upward_db", "Upward", ControlKind::Float, 0.0, 6.0, 0.0, 0.0, false, "dB", {}},
    {"mode", "mode", "Mode", ControlKind::Choice, 0.0, 1.0, 0.0, 0.0, false, "", kLimitModeLabels},
}};

// Stage 5's. Their defaults are neutral too: a Width and a Transient leave the track as it was, and a Multiband turns nothing down below
// 0 dBFS, playing its bands' sum, an allpass with No delay, and the track delayed with Linear phase.
inline constexpr std::array<const char*, 3> kFocusLabels{"Full", "Lows", "Highs"};

inline constexpr std::array<ControlSpec, 5> kWidthControls{{
    {"balance", "balance", "Balance", ControlKind::Float, -100.0, 100.0, 0.0, 0.0, false, "", {}},
    {"width", "width_percent", "Width", ControlKind::Float, 0.0, 200.0, 100.0, 0.0, true, "%", {}, false, BoostLaw::WidthPercent},
    {"mono_bass", "mono_bass", "Mono Bass", ControlKind::Bool, 0.0, 1.0, 0.0, 0.0, false, "", {}},
    {"mono_bass_freq", "mono_bass_hz", "Mono Bass Freq", ControlKind::Float, 40.0, 300.0, 120.0, kEqualRatios, false, "Hz", {}},
    {"bypass", "bypassed", "Bypass", ControlKind::Bool, 0.0, 1.0, 0.0, 0.0, false, "", {}},
}};

inline constexpr std::array<ControlSpec, 8> kTransientControls{{
    {"attack", "attack_db", "Attack", ControlKind::Float, -12.0, 12.0, 0.0, 0.0, true, "dB", {}},
    {"sustain", "sustain_db", "Sustain", ControlKind::Float, -12.0, 12.0, 0.0, 0.0, true, "dB", {}},
    {"speed", "speed_ms", "Speed", ControlKind::Float, 5.0, 50.0, 20.0, kEqualRatios, false, "ms", {}},
    {"focus", "focus", "Focus", ControlKind::Choice, 0.0, 2.0, 0.0, 0.0, false, "", kFocusLabels},
    {"mix", "mix_percent", "Mix", ControlKind::Float, 0.0, 100.0, 100.0, 0.0, false, "%", {}},
    {"output", "output_db", "Output", ControlKind::Float, -12.0, 12.0, 0.0, 0.0, true, "dB", {}},
    {"bypass", "bypassed", "Bypass", ControlKind::Bool, 0.0, 1.0, 0.0, 0.0, false, "", {}},
    // The owner's request of 2026-11-03, after the rest so that no other control's place moves. Its parameters go at the very end of
    // the host's list (kControlsAddedLater in Parameters.cpp). At -80, where it starts, it shapes every hit, as before it existed.
    {"threshold", "threshold_db", "Threshold", ControlKind::Float, -80.0, 0.0, -80.0, 0.0, false, "dB", {}},
}};

// -1 if there is none by that name. Usable at compile time, on the tables above.
constexpr int indexOfControl(std::span<const ControlSpec> controls, std::string_view field) noexcept {
    for (size_t i = 0; i < controls.size(); ++i)
        if (std::string_view(controls[i].field) == field) return static_cast<int>(i);
    return -1;
}

// Where a control is in one of the tables above, found while compiling: a name that isn't in it does not compile, where a -1 would
// be an index out of range in a release build.
consteval int tableControlIndex(std::span<const ControlSpec> controls, std::string_view field) {
    const int index = indexOfControl(controls, field);
    if (index < 0) throw "the table has no control by that name";
    return index;
}

consteval int dynamicControlIndex(std::string_view field) { return tableControlIndex(kDynamicControls, field); }

// Whether every control of a table that a switch keeps from raising the level raises it otherwise, and names a Bool of the same table.
consteval bool boostSwitchesAreBools(std::span<const ControlSpec> controls) {
    for (const ControlSpec& control : controls) {
        if (control.boostOnlyWhenOff == nullptr) continue;
        const int index = indexOfControl(controls, control.boostOnlyWhenOff);
        if (!control.raisesLevel || index < 0 || controls[static_cast<size_t>(index)].kind != ControlKind::Bool) return false;
    }
    return true;
}
static_assert(boostSwitchesAreBools(kEqControls) && boostSwitchesAreBools(kDynamicControls) && boostSwitchesAreBools(kDeEssControls)
              && boostSwitchesAreBools(kLimitControls) && boostSwitchesAreBools(kWidthControls) && boostSwitchesAreBools(kTransientControls));

// -1 if the type has none by that name. (indexOfControl, above, is the same at compile time.)
int controlIndex(ModuleType, const char* field);

// How the AI names a type: "eq", "dynamic", "gate", "deess", "drive", "limit", "width", "multiband", "transient", "resonance".
const char* typeName(ModuleType);

// Whether a type turns the track down and says how far: its module has a dsp::ReductionMeter, and the AI and the window read it.
// EQ, Drive and Width false; Dynamic, Gate, De-Ess, Limit, Multiband, Transient and Resonance true.
bool reportsReduction(ModuleType) noexcept;

// Whether a type turns the track up by its own action and says how far: its module has a boost meter, read as the reduction is.
// Transient alone.
bool reportsBoost(ModuleType) noexcept;

// Calls fn(index, spec) for each control, the one named "bypass" first and then the rest in the table's order. This is the
// order a slot's controls are written and reported in, so that its bypass goes before what it hides.
template <typename Fn>
void forEachControl(std::span<const ControlSpec> controls, Fn&& fn) {
    const auto visit = [&](bool bypass) {
        for (size_t index = 0; index < controls.size(); ++index)
            if ((std::string_view(controls[index].field) == "bypass") == bypass) fn(static_cast<int>(index), controls[index]);
    };
    visit(true);
    visit(false);
}

template <typename Fn>
void forEachControl(ModuleType type, Fn&& fn) {
    forEachControl(controlsOf(type), fn);
}

}  // namespace smartq
