#pragma once

#include "model/Documents.h"

namespace sqlive::model {

// Plain sentences about what could not be read as written and what was used instead.
struct ParseNotes { juce::StringArray notes; };

juce::var toVar(const BandDoc&);    bool fromVar(const juce::var&, BandDoc&, ParseNotes&);
juce::var toVar(const SongDoc&);    bool fromVar(const juce::var&, SongDoc&, ParseNotes&);
juce::var toVar(const SetlistDoc&); bool fromVar(const juce::var&, SetlistDoc&, ParseNotes&);
juce::var toVar(const RigDoc&);     bool fromVar(const juce::var&, RigDoc&, ParseNotes&);

// The MIDI parts on their own, with the same limits as inside a document (commands, test builders and the MIDI import use
// these and never parse or limit them again). fromVar returns false when the var isn't an object, or for a message, when
// it has no type this version knows; anything else is limited and noted. A marker drops messages of an unknown type.
juce::var toVar(const Patch&);          bool fromVar(const juce::var&, Patch&, ParseNotes&);
juce::var toVar(const MidiMessageDoc&); bool fromVar(const juce::var&, MidiMessageDoc&, ParseNotes&);
juce::var toVar(const MarkerDoc&);      bool fromVar(const juce::var&, MarkerDoc&, ParseNotes&);
juce::var toVar(const SectionDoc&);     bool fromVar(const juce::var&, SectionDoc&, ParseNotes&);
juce::var toVar(const ControlBinding&); bool fromVar(const juce::var&, ControlBinding&, ParseNotes&);

// fromVar and fromJson return false only when the text is not a JSON object; anything else is repaired and noted.
template <typename Doc> juce::String toJson(const Doc& doc) { return juce::JSON::toString(toVar(doc), false); }
template <typename Doc> bool fromJson(const juce::String& text, Doc& doc, ParseNotes& notes) {
    juce::var v;
    if (! juce::JSON::parse(text, v).wasOk()) return false;
    return fromVar(v, doc, notes);
}

juce::String newId();   // juce::Uuid().toDashedString()
// The id a document's JSON text gives itself; "" when it gives none (the band has none) or the text isn't JSON.
juce::String idInText(const juce::String& text);

}  // namespace sqlive::model
