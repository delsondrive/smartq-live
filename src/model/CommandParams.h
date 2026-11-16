#pragma once

#include <algorithm>
#include <optional>
#include <vector>

#include "model/CommandLayer.h"

// What every command file shares: reading a call's parameters, refusing in a plain sentence, and finding a thing by id.
namespace sqlive::model::commands {

struct Refusal { juce::String message; };

[[noreturn]] void refuse(const juce::String& message);

std::optional<double> asNumber(const juce::var& v);

// The parameters of one call. A parameter is "given" when it is present and not void.
class Params {
public:
    explicit Params(const juce::var& v) : all_(v) {}

    bool given(const char* key) const { return present(key) && ! raw(key).isVoid(); }
    bool present(const char* key) const { return all_.hasProperty(juce::Identifier(key)); }
    juce::var raw(const char* key) const { return all_[juce::Identifier(key)]; }

    juce::String id(const char* key) const;
    juce::String text(const char* key) const;
    // A number, limited to lo..hi the way the JSON reader limits it.
    double number(const char* key, double lo, double hi) const;
    int whole(const char* key, int lo, int hi) const;
    float db(const char* key) const;
    bool flag(const char* key) const;

private:
    juce::var all_;
};

// The one place that says "that thing no longer exists".
template <typename T> T& found(T* thing, const char* what) {
    if (thing == nullptr) refuse("That " + juce::String(what) + " no longer exists.");
    return *thing;
}
template <typename List, typename Pred> auto* findIn(List& list, Pred&& pred) {
    auto it = std::find_if(list.begin(), list.end(), pred);
    return it == list.end() ? nullptr : &*it;
}

SongDoc& songOf(BandModel& m, const Params& p);
SetlistDoc& setlistOf(BandModel& m, const Params& p);
RigDoc& rigOf(BandModel& m, const Params& p);

juce::String nonEmpty(const juce::String& s, const juce::String& message);
juce::Colour parseColour(const juce::String& text);

template <typename T, typename Pred> void eraseIf(std::vector<T>& v, Pred&& pred) {
    v.erase(std::remove_if(v.begin(), v.end(), pred), v.end());
}

// Registers a command whose handler takes Params and may refuse; a refusal becomes CommandResult::failure.
template <typename Fn> void addCommand(CommandLayer& layer, const char* name, Fn fn) {
    layer.registerCommand(name, [fn](BandModel& m, const juce::var& raw) {
        try { return fn(m, Params(raw)); }
        catch (const Refusal& r) { return CommandResult::failure(r.message); }
    });
}

}  // namespace sqlive::model::commands
