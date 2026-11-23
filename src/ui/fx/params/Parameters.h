#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <span>

#include "dsp/DeEssModule.h"
#include "dsp/DynamicModule.h"
#include "dsp/LimitModule.h"
#include "dsp/TransientModule.h"
#include "dsp/Types.h"
#include "dsp/WidthModule.h"
#include "ui/fx/params/ParameterRanges.h"
#include "ui/fx/params/Slots.h"

// Copied from Smart Q (smarteq_vst src/plugin/Parameters.h) and adapted for Smart Q Live: the six kinds Live plays, a layout made for
// the slots of one chain, and none of the plug-in's saved state properties (Live keeps its effects in its own documents).

namespace smartq {

// Parameter IDs keep Smart Q's scheme.
inline constexpr int kParameterVersion = 1;
inline constexpr const char* kOutputGainId = "output_gain";
inline constexpr const char* kBypassId = "bypass";  // the plug-in's own Bypass, which Live doesn't have: always off

inline constexpr double kMaxOutputGainDb = ranges::kMaxOutputGainDb;

// The parameters of these slots, each in its table's order (an EQ's bands first), and the plug-in's Bypass, off. Each slot once.
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout(std::span<const int> slots);

// The items of the "shape", "channel" and "listen" choice parameters, in the order of dsp::Shape, dsp::ChannelMode and dsp::ListenTo.
juce::StringArray shapeNames();
juce::StringArray channelNames();
juce::StringArray listenNames();

// "eq2_band01_gain": eq and band are 0-based, field is one of enabled, shape, freq, gain, q, slope, channel,
// dynamic, threshold, range, attack, release, listen, bypass.
juce::String bandParamId(int eq, int band, const char* field);

// "eq1_output_gain": a parameter of a slot as a whole. The field is a control's, from ui/fx/params/ModuleControls.h.
juce::String moduleParamId(int slot, const char* field);

// Raw-value pointers for one band, found once so that the audio thread reads them without lookups.
struct BandParams {
    std::atomic<float>* enabled = nullptr;
    std::atomic<float>* shape = nullptr;
    std::atomic<float>* frequency = nullptr;
    std::atomic<float>* gain = nullptr;
    std::atomic<float>* q = nullptr;
    std::atomic<float>* slope = nullptr;
    std::atomic<float>* channel = nullptr;
    std::atomic<float>* dynamic = nullptr;
    std::atomic<float>* threshold = nullptr;
    std::atomic<float>* range = nullptr;
    std::atomic<float>* attack = nullptr;
    std::atomic<float>* release = nullptr;
    std::atomic<float>* listen = nullptr;
    std::atomic<float>* bypass = nullptr;

    dsp::BandSettings read() const noexcept;  // any thread, no allocation
};

BandParams lookupBandParams(const juce::AudioProcessorValueTreeState&, int eq, int band);

dsp::BandSettings readBand(const juce::AudioProcessorValueTreeState&, int eq, int band);

// The most controls a type's table may have, besides an EQ's bands.
inline constexpr size_t kMaxModuleControls = 64;
using ControlValues = std::array<std::atomic<float>*, kMaxModuleControls>;

// Raw-value pointers for every control of a slot, by the control's place in its type's table, found once so that the audio thread
// reads them without lookups.
ControlValues lookupControlValues(const juce::AudioProcessorValueTreeState&, int slot);

// A slot's control values, read as its module's settings.
template <typename Settings>
struct ModuleParams {
    ControlValues values{};

    Settings read() const noexcept;  // any thread, no allocation
};

using DynamicParams = ModuleParams<dsp::DynamicSettings>;
using DeEssParams = ModuleParams<dsp::DeEssSettings>;  // never listening: Listen isn't a parameter
using LimitParams = ModuleParams<dsp::LimitSettings>;
using WidthParams = ModuleParams<dsp::WidthSettings>;
using TransientParams = ModuleParams<dsp::TransientSettings>;

template <> dsp::DynamicSettings DynamicParams::read() const noexcept;
template <> dsp::DeEssSettings DeEssParams::read() const noexcept;
template <> dsp::LimitSettings LimitParams::read() const noexcept;
template <> dsp::WidthSettings WidthParams::read() const noexcept;
template <> dsp::TransientSettings TransientParams::read() const noexcept;

// `slot` is a Dynamic's slot.
DynamicParams lookupDynamicParams(const juce::AudioProcessorValueTreeState&, int slot);

// Each `slot` is one of that type's slots.
dsp::DynamicSettings readDynamic(const juce::AudioProcessorValueTreeState&, int slot);
dsp::DeEssSettings readDeEss(const juce::AudioProcessorValueTreeState&, int slot);
dsp::LimitSettings readLimit(const juce::AudioProcessorValueTreeState&, int slot);
dsp::WidthSettings readWidth(const juce::AudioProcessorValueTreeState&, int slot);
dsp::TransientSettings readTransient(const juce::AudioProcessorValueTreeState&, int slot);

// Message thread. Each parameter that changes is in a change gesture, and all of them in one gesture together (Live: every
// gesture is opened before any value moves and closed after the last, so the band's whole change is one edit, one undo step).
void writeBand(juce::AudioProcessorValueTreeState&, int eq, int band, const dsp::BandSettings&);

// A series of edits to one band of one EQ that the host records as one, such as a mouse drag. A parameter's
// gesture opens at its first real change and stays open until end(), so a click that never moves
// tells the host nothing and a drag writes one gesture per parameter it touched. Unchanged values
// stay silent, as in writeBand. Message thread.
class BandEditGesture {
public:
    explicit BandEditGesture(juce::AudioProcessorValueTreeState& apvts, int eq) : apvts_(apvts), eq_(eq) {}
    ~BandEditGesture() { end(); }

    BandEditGesture(const BandEditGesture&) = delete;
    BandEditGesture& operator=(const BandEditGesture&) = delete;

    // Closes any gesture still open first.
    void begin(int band);
    void write(const dsp::BandSettings&);
    void end();

    bool active() const noexcept { return band_ >= 0; }
    int band() const noexcept { return band_; }

private:
    juce::AudioProcessorValueTreeState& apvts_;
    int eq_;
    int band_ = -1;
    std::array<juce::RangedAudioParameter*, 14> open_{};
    size_t openCount_ = 0;
};

}  // namespace smartq
