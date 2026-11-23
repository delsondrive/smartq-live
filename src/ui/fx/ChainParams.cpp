#include "ui/fx/ChainParams.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "model/Effects.h"
#include "ui/fx/params/ModuleControls.h"
#include "ui/fx/params/ParameterRanges.h"
#include "ui/fx/params/Parameters.h"

namespace sqlive::ui::fx {

namespace {

using smartq::ModuleType;

// An EQ band's fields, as its parameters and the effect's band objects name them ("enabled" is the parameters' alone: a band in
// the list is in use).
constexpr std::array<const char*, 13> kBandFields{"shape", "freq", "gain", "q", "slope", "channel", "dynamic",
                                                  "threshold", "range", "attack", "release", "listen", "bypass"};

// A Choice's or a switch's value, as its parameter takes it: the index, rounded; 0 or 1.
double asStep(const juce::var& v) { return (double) std::lround((double) v); }

bool isNumber(const juce::var* v) {
    return v != nullptr && (v->isInt() || v->isInt64() || v->isDouble() || v->isBool()) && std::isfinite((double) *v);
}

// Smart Q's slope choice nearest these dB per octave (as model/Effects.cpp writes a band's "slope").
int slopeIndexOf(int slopeDbPerOct) {
    int best = 0;
    for (int i = 1; i < (int) smartq::kSlopeChoices.size(); ++i)
        if (std::abs(smartq::kSlopeChoices[(size_t) i] - slopeDbPerOct) < std::abs(smartq::kSlopeChoices[(size_t) best] - slopeDbPerOct))
            best = i;
    return best;
}

}  // namespace

std::optional<ModuleType> moduleTypeOf(const juce::String& kind) {
    if (kind == "eq") return ModuleType::Eq;
    if (kind == "dynamic") return ModuleType::Dynamic;
    if (kind == "deess") return ModuleType::DeEss;
    if (kind == "width") return ModuleType::Width;
    if (kind == "transient") return ModuleType::Transient;
    if (kind == "limit") return ModuleType::Limit;
    return std::nullopt;
}

ChainParams::ChainParams(const std::vector<model::EffectDoc>& effects) : effects_(effects), bandsAt_(effects.size()) {
    std::array<int, 10> used{};   // by ModuleType: slots taken so far
    for (const auto& effect : effects) {
        const auto type = moduleTypeOf(effect.kind);
        slots_.push_back(type ? smartq::slotOf(*type, used[(size_t) *type]++) : -1);   // -1 past four of a kind
    }
    const auto present = slots();
    apvts_ = std::make_unique<juce::AudioProcessorValueTreeState>(*this, nullptr, "SmartQLiveEffects",
                                                                   smartq::createParameterLayout(present));

    bindings_.resize((size_t) getParameters().size());
    const auto bind = [this](const juce::String& id, Binding binding) {
        if (auto* p = parameter(id)) bindings_[(size_t) p->getParameterIndex()] = std::move(binding);
    };
    for (int index = 0; index < (int) slots_.size(); ++index) {
        const int slot = slots_[(size_t) index];
        if (slot < 0) continue;
        const ModuleType type = smartq::slotInfo(slot).type;
        if (type == ModuleType::Eq)
            for (int band = 0; band < smartq::dsp::kMaxBands; ++band) {
                bind(smartq::bandParamId(smartq::slotInfo(slot).typeIndex, band, "enabled"), {index, band, "enabled"});
                for (const char* field : kBandFields)
                    bind(smartq::bandParamId(smartq::slotInfo(slot).typeIndex, band, field), {index, band, field});
            }
        for (const auto& control : smartq::controlsOf(type)) bind(smartq::moduleParamId(slot, control.field), {index, -1, control.field});
    }
    for (auto* p : getParameters()) p->addListener(this);
    load(effects);
}

ChainParams::~ChainParams() {
    for (auto* p : getParameters()) p->removeListener(this);
}

bool ChainParams::fits(const std::vector<model::EffectDoc>& effects) const {
    return effects.size() == effects_.size()
           && std::equal(effects.begin(), effects.end(), effects_.begin(), [](const auto& a, const auto& b) { return a.kind == b.kind; });
}

int ChainParams::slotOf(int index) const noexcept {
    return index >= 0 && index < (int) slots_.size() ? slots_[(size_t) index] : -1;
}

int ChainParams::indexOfSlot(int slot) const noexcept {
    if (slot < 0) return -1;
    const auto found = std::find(slots_.begin(), slots_.end(), slot);
    return found != slots_.end() ? (int) (found - slots_.begin()) : -1;
}

std::vector<int> ChainParams::slots() const {
    std::vector<int> present;
    for (const int slot : slots_)
        if (slot >= 0) present.push_back(slot);
    return present;
}

juce::RangedAudioParameter* ChainParams::parameter(const juce::String& id) const { return apvts_->getParameter(id); }

void ChainParams::set(juce::RangedAudioParameter* p, double value) {
    if (p == nullptr) return;
    const float normalised = p->convertTo0to1((float) value);
    if (std::abs(normalised - p->getValue()) > 1.0e-6f) p->setValueNotifyingHost(normalised);
}

// ---- Loading ------------------------------------------------------------------------------------------------------------

void ChainParams::load(const std::vector<model::EffectDoc>& effects) {
    if (!fits(effects)) return;
    const juce::ScopedValueSetter<bool> quiet(loading_, true);
    effects_ = effects;
    for (int index = 0; index < (int) slots_.size(); ++index) {
        if (slots_[(size_t) index] < 0) continue;
        loadModule(index);
        if (smartq::slotInfo(slots_[(size_t) index]).type == ModuleType::Eq) loadBands(index);
    }
}

// Each control from the effect's value by its field (one missing, or not a number or a switch: the kind's default), and
// "bypass" from `on`.
void ChainParams::loadModule(int index) {
    const int slot = slots_[(size_t) index];
    const auto& effect = effects_[(size_t) index];
    for (const auto& control : smartq::controlsOf(smartq::slotInfo(slot).type)) {
        auto* p = parameter(smartq::moduleParamId(slot, control.field));
        if (juce::String(control.field) == "bypass") {
            set(p, effect.on ? 0.0 : 1.0);
            continue;
        }
        const juce::var* v = effect.values.getVarPointer(juce::Identifier(control.field));
        double value = isNumber(v) ? (double) *v : control.defaultValue;
        if (control.kind != smartq::ControlKind::Float) value = asStep(value);
        set(p, value);
    }
}

// The bands of the effect's list (its band objects, the first 24, as the engine plays them) each in the band it was in, in the
// list's order; one more than before takes the first band free. Every other band is not in use.
void ChainParams::loadBands(int index) {
    const int eq = smartq::slotInfo(slots_[(size_t) index]).typeIndex;
    auto& effect = effects_[(size_t) index];
    juce::Array<juce::var> bands;
    if (const auto* list = effect.values.getVarPointer("bands"); list != nullptr && list->isArray()) {
        for (const auto& band : *list->getArray())
            if (band.getDynamicObject() != nullptr && bands.size() < smartq::dsp::kMaxBands) bands.add(band);
        if (bands.size() != list->getArray()->size()) effect.values.set("bands", juce::var(bands));   // what plays, and no more
    }
    const auto settings = std::get<engine::EqSettings>(model::settingsOf(effect, model::EffectPlace::Part));

    auto& at = bandsAt_[(size_t) index];
    std::array<bool, smartq::dsp::kMaxBands> taken{};
    std::vector<int> next;
    for (int k = 0; k < bands.size(); ++k) {
        int band = k < (int) at.size() ? at[(size_t) k] : -1;
        if (band < 0 || taken[(size_t) band])
            band = (int) (std::find(taken.begin(), taken.end(), false) - taken.begin());
        taken[(size_t) band] = true;
        next.push_back(band);
    }
    at = next;

    const auto id = [eq](int band, const char* field) { return smartq::bandParamId(eq, band, field); };
    for (int band = 0; band < smartq::dsp::kMaxBands; ++band) {
        if (!taken[(size_t) band]) {
            set(parameter(id(band, "enabled")), 0.0);
            continue;
        }
        const auto k = (size_t) (std::find(at.begin(), at.end(), band) - at.begin());
        const smartq::dsp::BandSettings& b = settings.bands[k];
        set(parameter(id(band, "shape")), (double) (int) b.shape);
        set(parameter(id(band, "freq")), b.frequencyHz);
        set(parameter(id(band, "gain")), b.gainDb);
        set(parameter(id(band, "q")), b.q);
        set(parameter(id(band, "slope")), (double) slopeIndexOf(b.slopeDbPerOct));
        set(parameter(id(band, "channel")), (double) (int) b.channel);
        set(parameter(id(band, "dynamic")), b.dynamic ? 1.0 : 0.0);
        set(parameter(id(band, "threshold")), b.thresholdDb);
        set(parameter(id(band, "range")), b.rangeDb);
        set(parameter(id(band, "attack")), b.attackMs);
        set(parameter(id(band, "release")), b.releaseMs);
        set(parameter(id(band, "listen")), (double) (int) b.listen);
        set(parameter(id(band, "bypass")), b.bypassed ? 1.0 : 0.0);
        set(parameter(id(band, "enabled")), 1.0);
    }
}

// ---- Edits --------------------------------------------------------------------------------------------------------------

// A switch as a bool, a Choice as its index, a Float as the value it holds.
juce::var ChainParams::valueOf(const juce::RangedAudioParameter& p) const {
    if (auto* b = dynamic_cast<const juce::AudioParameterBool*>(&p)) return b->get();
    if (auto* c = dynamic_cast<const juce::AudioParameterChoice*>(&p)) return c->getIndex();
    return (double) p.convertFrom0to1(p.getValue());
}

// A band's object, as model/Effects.cpp writes one, from the band's parameters.
juce::var ChainParams::bandFromParameters(int eq, int band) const {
    auto* object = new juce::DynamicObject();
    for (const char* field : kBandFields)
        if (auto* p = parameter(smartq::bandParamId(eq, band, field))) object->setProperty(field, valueOf(*p));
    return juce::var(object);
}

// The change into the effect; false when it leaves the effect as it was. A band's object is never changed in place: the
// effect's list and its objects may be shared with the documents.
bool ChainParams::apply(const Binding& binding, const juce::RangedAudioParameter& p, model::EffectDoc& effect) {
    const juce::var value = valueOf(p);
    if (binding.band < 0) {
        if (binding.field == "bypass") {
            const bool on = !(bool) value;
            if (effect.on == on) return false;
            effect.on = on;
            return true;
        }
        const juce::Identifier field(binding.field);
        const juce::var* old = effect.values.getVarPointer(field);
        if (old != nullptr && isNumber(old) && (double) *old == (double) value) return false;
        effect.values.set(field, value);
        return true;
    }

    const int eq = smartq::slotInfo(slots_[(size_t) binding.index]).typeIndex;
    auto& at = bandsAt_[(size_t) binding.index];
    const auto found = std::find(at.begin(), at.end(), binding.band);
    const int k = found != at.end() ? (int) (found - at.begin()) : -1;
    juce::Array<juce::var> bands;
    if (const auto* list = effect.values.getVarPointer("bands"); list != nullptr && list->isArray()) bands = *list->getArray();

    if (binding.field == "enabled") {
        if ((bool) value == (k >= 0)) return false;
        if ((bool) value) {
            at.push_back(binding.band);
            bands.add(bandFromParameters(eq, binding.band));
        } else {
            at.erase(found);
            bands.remove(k);
        }
    } else {
        if (k < 0 || k >= bands.size()) return false;   // a band not in use is no part of the effect
        auto* object = bands[k].getDynamicObject();
        const juce::Identifier field(binding.field);
        if (object != nullptr && isNumber(object->getProperties().getVarPointer(field)) && (double) object->getProperty(field) == (double) value)
            return false;
        auto copy = object != nullptr ? object->clone() : std::make_unique<juce::DynamicObject>();
        copy->setProperty(field, value);
        bands.set(k, juce::var(copy.release()));
    }
    effect.values.set("bands", juce::var(bands));
    return true;
}

void ChainParams::parameterGestureChanged(int, bool gestureIsStarting) {
    if (gestureIsStarting) {
        if (openGestures_++ == 0) gestureId_ = juce::Uuid().toString();
    } else if (openGestures_ > 0) {
        --openGestures_;
    }
}

void ChainParams::parameterValueChanged(int parameterIndex, float) {
    if (loading_ || parameterIndex < 0 || parameterIndex >= (int) bindings_.size()) return;
    const Binding& binding = bindings_[(size_t) parameterIndex];
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(getParameters()[parameterIndex]);
    if (binding.index < 0 || p == nullptr) return;
    auto effect = effects_[(size_t) binding.index];
    if (!apply(binding, *p, effect)) return;
    effects_[(size_t) binding.index] = effect;
    const juce::String gesture = openGestures_ > 0 ? gestureId_ : juce::Uuid().toString();
    if (onEdited) onEdited(binding.index, effect, gesture);
}

}  // namespace sqlive::ui::fx
