#pragma once

#include "model/CommandLayer.h"

namespace sqlive::model {

// The band's documents by name, as the commands that set many of them at once take them ("folder.reload", "snapshot.apply")
// and as a snapshot keeps them: "band:", "song:<id>", "setlist:<id>", "rig:<id>".
juce::String keyName(const DocKey&);
bool parseKeyName(const juce::String&, DocKey&);                 // false for a name that isn't one of those

// The text is a document of this kind (fromJson repairs anything but text that isn't a JSON object).
bool readableAs(DocKind, const juce::String& text);

// {documents: {"<kind>:<id>": JSON text, or "" to remove}}: sets each document from its text (editBand() before the band's
// applyText, as applyText sets the band without noting it as changed). unreadable: the refusal when a text can't be read, so
// each command says it its own way. A command's handler: on a refusal, CommandLayer puts back whatever was set.
CommandResult applyDocuments(BandModel&, const juce::var& params, const juce::String& unreadable);

}  // namespace sqlive::model
