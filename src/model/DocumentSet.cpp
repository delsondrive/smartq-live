#include "model/DocumentSet.h"

#include "model/DocumentsJson.h"

namespace sqlive::model {

namespace {

const char* kindName(DocKind kind) {
    switch (kind) {
    case DocKind::Band: return "band";
    case DocKind::Song: return "song";
    case DocKind::Setlist: return "setlist";
    case DocKind::Rig: return "rig";
    }
    return "";
}

}  // namespace

juce::String keyName(const DocKey& key) { return juce::String(kindName(key.kind)) + ":" + key.id; }

bool parseKeyName(const juce::String& name, DocKey& key) {
    const auto kind = name.upToFirstOccurrenceOf(":", false, false);
    for (auto k : {DocKind::Band, DocKind::Song, DocKind::Setlist, DocKind::Rig}) {
        if (kind != kindName(k)) continue;
        key = {k, name.fromFirstOccurrenceOf(":", false, false)};
        return name.containsChar(':') && (k == DocKind::Band) == key.id.isEmpty();
    }
    return false;
}

bool readableAs(DocKind kind, const juce::String& text) {
    ParseNotes notes;
    switch (kind) {
    case DocKind::Band: { BandDoc d; return fromJson(text, d, notes); }
    case DocKind::Song: { SongDoc d; return fromJson(text, d, notes); }
    case DocKind::Setlist: { SetlistDoc d; return fromJson(text, d, notes); }
    case DocKind::Rig: { RigDoc d; return fromJson(text, d, notes); }
    }
    return false;
}

CommandResult applyDocuments(BandModel& m, const juce::var& params, const juce::String& unreadable) {
    const auto* documents = params.getProperty("documents", {}).getDynamicObject();
    if (documents == nullptr) return CommandResult::failure("There was nothing to load.");
    for (const auto& property : documents->getProperties()) {
        DocKey key;
        if (!parseKeyName(property.name.toString(), key))
            return CommandResult::failure("There is no document called " + property.name.toString() + ".");
        const auto text = property.value.toString();
        if (text.isNotEmpty() && !readableAs(key.kind, text)) return CommandResult::failure(unreadable);
        if (key.kind == DocKind::Band) {
            if (text.isEmpty()) return CommandResult::failure("A band can't be without its band.json.");
            m.editBand();                                        // applyText sets the band without noting it as changed
        }
        m.applyText(key, text);
    }
    return CommandResult::success();
}

}  // namespace sqlive::model
