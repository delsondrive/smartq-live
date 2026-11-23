#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "model/Documents.h"
#include "ui/fx/params/Slots.h"

namespace sqlive::ui::fx {

// The Smart Q type of an effect kind ("eq", "dynamic", "deess", "width", "transient", "limit"); none for a kind not known.
std::optional<smartq::ModuleType> moduleTypeOf(const juce::String& kind);

// One chain's effects as Smart Q's module pages read and change them: a hidden juce::AudioProcessor that only hosts a
// juce::AudioProcessorValueTreeState with Smart Q's parameters (ui/fx/params/Parameters.h) for the slots of this chain, so that
// the copied pages bind to it as they bind to the plug-in's.
//
// Slots: each effect of a kind known gets one, its kind's next in Smart Q's scheme ("eq1", "dynamic1", "eq2", ...; four of each, so
// a chain of four of one kind fits), in the chain's order; ids are "<slotId>_<field>" and an EQ's bands "eq1_band01_gain". An effect
// of a kind not known has none. The layout is fixed when this is made: a chain whose kinds or order change is another ChainParams.
//
// Values: a module's are its effect's values by Smart Q's field names (a Choice as its index), its "bypass" is the effect's `on`
// turned over, and an EQ's bands are the effect's "bands" list, each in a band of its own (the list's first in band 1 when made, and
// after that each keeps its band; a band in use is "enabled"). The plug-in's own "bypass" is there, and always off.
//
// Message thread.
class ChainParams final : public juce::AudioProcessor, private juce::AudioProcessorParameter::Listener {
public:
    explicit ChainParams(const std::vector<model::EffectDoc>& effects);
    ~ChainParams() override;

    juce::AudioProcessorValueTreeState& apvts() noexcept { return *apvts_; }

    // Whether these effects are of the kinds this was made for, in the same order.
    bool fits(const std::vector<model::EffectDoc>&) const;

    // Sets every value to these effects' (a value missing: its kind's default), sending nothing to onEdited; each parameter that
    // changes tells its page. Effects that don't fit are ignored.
    void load(const std::vector<model::EffectDoc>&);

    // The effects as they are now: as loaded, with every edit since.
    const std::vector<model::EffectDoc>& effects() const noexcept { return effects_; }

    // An effect's slot, by its index in the chain: -1 for one of a kind not known, or no such index. And back: -1 for a slot not here.
    int slotOf(int index) const noexcept;
    int indexOfSlot(int slot) const noexcept;
    // The slots here, in the chain's order.
    std::vector<int> slots() const;

    // One call for each change of a value made here (by a page; not by load): the effect's index, the effect with the change, and
    // the change's gesture: one id for every change from the first parameter gesture opening until the last open one closes (a
    // drag, an EQ band's whole edit), and a new one for each such run and for each change made outside any gesture. A change
    // that leaves the effect as it was (a field of an EQ band not in use) is not told.
    std::function<void(int index, const model::EffectDoc&, const juce::String& gestureId)> onEdited;

    // ---- juce::AudioProcessor: none of it is used; nothing is played here.
    const juce::String getName() const override { return "Smart Q Live effects"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    // What a parameter is of: the effect's index, the band for an EQ band's (-1 for a module's own), and the field.
    struct Binding {
        int index = -1, band = -1;
        juce::String field;
    };

    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;

    juce::RangedAudioParameter* parameter(const juce::String& id) const;
    void set(juce::RangedAudioParameter*, double value);   // only when it differs
    void loadModule(int index);
    void loadBands(int index);
    juce::var valueOf(const juce::RangedAudioParameter&) const;
    juce::var bandFromParameters(int eq, int band) const;
    bool apply(const Binding&, const juce::RangedAudioParameter&, model::EffectDoc&);

    std::vector<model::EffectDoc> effects_;
    std::vector<int> slots_;                         // by effect index
    std::vector<std::vector<int>> bandsAt_;          // by effect index: the band each of an EQ's "bands" is in
    std::unique_ptr<juce::AudioProcessorValueTreeState> apvts_;
    std::vector<Binding> bindings_;                  // by the parameter's index in this processor
    bool loading_ = false;
    int openGestures_ = 0;
    juce::String gestureId_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainParams)
};

}  // namespace sqlive::ui::fx
