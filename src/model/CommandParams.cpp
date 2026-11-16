#include "model/CommandParams.h"

#include <cmath>

#include "model/Units.h"

namespace sqlive::model::commands {

using juce::String;
using juce::var;

void refuse(const String& message) { throw Refusal{message}; }

std::optional<double> asNumber(const var& v) {
    if (v.isInt() || v.isInt64() || v.isDouble()) {
        const double d = (double) v;
        return std::isfinite(d) ? std::optional<double>(d) : std::nullopt;
    }
    if (v.isString()) {
        auto s = v.toString().trim();
        // At least one digit: "-", "e" and "." alone are not numbers, though getDoubleValue would make 0 of them.
        if (s.containsAnyOf("0123456789") && s.containsOnly("0123456789.+-eE")) {
            const double d = s.getDoubleValue();
            if (std::isfinite(d)) return d;
        }
    }
    return std::nullopt;
}

String Params::id(const char* key) const { auto v = raw(key); return v.isString() ? v.toString() : String(); }
String Params::text(const char* key) const {
    auto v = raw(key);
    if (! v.isString()) refuse(String(key) + " must be text.");
    return v.toString().trim();
}
double Params::number(const char* key, double lo, double hi) const {
    auto n = asNumber(raw(key));
    if (! n) refuse(String(key) + " must be a number.");
    return std::min(hi, std::max(lo, *n));
}
int Params::whole(const char* key, int lo, int hi) const { return (int) std::round(number(key, lo, hi)); }
float Params::db(const char* key) const {
    auto n = asNumber(raw(key));
    if (! n) refuse(String(key) + " must be a number.");
    return clampDb((float) *n);
}
bool Params::flag(const char* key) const {
    auto v = raw(key);
    if (v.isBool()) return (bool) v;
    if (v.isString() && (v.toString() == "true" || v.toString() == "false")) return v.toString() == "true";
    refuse(String(key) + " must be true or false.");
}

SongDoc& songOf(BandModel& m, const Params& p) { return found(m.editSong(p.id("songId")), "song"); }
SetlistDoc& setlistOf(BandModel& m, const Params& p) { return found(m.editSetlist(p.id("setlistId")), "setlist"); }
RigDoc& rigOf(BandModel& m, const Params& p) { return found(m.editRig(p.id("rigId")), "rig"); }

String nonEmpty(const String& s, const String& message) { if (s.isEmpty()) refuse(message); return s; }

juce::Colour parseColour(const String& text) {
    auto s = text.trim().trimCharactersAtStart("#");
    if (s.length() != 6 || ! s.containsOnly("0123456789abcdefABCDEF")) refuse("A colour is six hex digits, like ff9a68.");
    return juce::Colour((juce::uint32) (0xff000000u | (juce::uint32) s.getHexValue32()));
}

}  // namespace sqlive::model::commands
