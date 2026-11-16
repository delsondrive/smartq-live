#pragma once

#include "model/Documents.h"

namespace sqlive::model {

// Plain sentences about what could not be read as written and what was used instead.
struct ParseNotes { juce::StringArray notes; };

juce::var toVar(const BandDoc&);    bool fromVar(const juce::var&, BandDoc&, ParseNotes&);
juce::var toVar(const SongDoc&);    bool fromVar(const juce::var&, SongDoc&, ParseNotes&);
juce::var toVar(const SetlistDoc&); bool fromVar(const juce::var&, SetlistDoc&, ParseNotes&);
juce::var toVar(const RigDoc&);     bool fromVar(const juce::var&, RigDoc&, ParseNotes&);

// fromVar and fromJson return false only when the text is not a JSON object; anything else is repaired and noted.
template <typename Doc> juce::String toJson(const Doc& doc) { return juce::JSON::toString(toVar(doc), false); }
template <typename Doc> bool fromJson(const juce::String& text, Doc& doc, ParseNotes& notes) {
    juce::var v;
    if (! juce::JSON::parse(text, v).wasOk()) return false;
    return fromVar(v, doc, notes);
}

juce::String newId();   // juce::Uuid().toDashedString()

}  // namespace sqlive::model
