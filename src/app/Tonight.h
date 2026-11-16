#pragma once

#include <compare>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include <juce_core/juce_core.h>

#include "model/Documents.h"

namespace sqlive::app {

// One level a fader sets: a part's gain, a song's click offset, a send in an output's mix, an output's trim, or one of the
// band's click, cues and pad levels.
struct LevelTarget {
    enum class Kind { Part, SongClick, Mix, OutputTrim, BandClick, BandCues, BandPad } kind;
    juce::String songId, partId, outputId, roleId;                // whichever the kind needs
    bool operator==(const LevelTarget&) const = default;
    // Written by hand, like model::DocKey's: juce::String has no <=>, so a defaulted one would be deleted.
    // Compares kind, then songId, partId, outputId, roleId with juce::String::compare.
    std::strong_ordering operator<=>(const LevelTarget&) const;
};

// The levels changed in Show mode, kept outside the band folder. Plain data with JSON, so it survives a restart.
// A change for something that no longer exists is kept, and simply finds nothing to apply to.
class Tonight {
public:
    void setLevel(const LevelTarget&, std::optional<float> db);   // nullopt = not sent (Mix only; ignored for the others)
    void setMute(const juce::String& songId, const juce::String& partId, bool);
    bool has(const LevelTarget&) const;
    bool hasMute(const juce::String& songId, const juce::String& partId) const;
    bool isEmpty() const;
    void clear();

    model::SongDoc apply(model::SongDoc) const;                   // its parts' gains and mutes, and its click offset
    model::RigDoc apply(model::RigDoc) const;                     // its outputs' mixes and trims
    model::BandDoc apply(model::BandDoc) const;                   // the click, cues and pad levels

    juce::String toJson() const;
    static Tonight fromJson(const juce::String&);                 // empty when it can't be read

    // The commands that would make these changes permanent, as (name, params); mixes and trims for the rig given.
    std::vector<std::pair<juce::String, juce::var>> asCommands(const juce::String& rigId) const;

private:
    std::map<LevelTarget, std::optional<float>> levels_;
    std::map<LevelTarget, bool> mutes_;                           // keyed by a Part target
};

}  // namespace sqlive::app
