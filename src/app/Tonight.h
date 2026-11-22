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

// The levels, mutes and pans changed in Show mode, kept outside the band folder. Plain data with JSON, so it survives a
// restart.
// A change for something that no longer exists is kept, and simply finds nothing to apply to.
class Tonight {
public:
    void setLevel(const LevelTarget&, std::optional<float> db);   // nullopt = not sent (Mix only; ignored for the others)
    void setMute(const juce::String& songId, const juce::String& partId, bool);
    // A Part target: the song's pan for that part. A Mix target: an own pan for that role on that output (a role that
    // followed the song stops following it, tonight only). Limited to -100..100; other kinds are ignored. Tonight never
    // links a role again: linking is offered in Edit mode only.
    void setPan(const LevelTarget&, int pan);
    bool has(const LevelTarget&) const;
    bool hasMute(const juce::String& songId, const juce::String& partId) const;
    bool hasPan(const LevelTarget&) const;
    bool isEmpty() const;
    int count() const;                                            // the things changed; a part whose gain, mute and pan all
                                                                  // changed counts once
    void clear();

    model::SongDoc apply(model::SongDoc) const;                   // its parts' gains, mutes and pans, and its click offset
    model::RigDoc apply(model::RigDoc) const;                     // its outputs' mixes, trims and own pans
    model::BandDoc apply(model::BandDoc) const;                   // the click, cues and pad levels

    juce::String toJson() const;
    static Tonight fromJson(const juce::String&);                 // empty when it can't be read

    // The commands that would make these changes permanent, as (name, params); mixes, trims and own pans for the rig given.
    // A part's pan is part.set's "pan"; an own pan is mix.setPan, which unlinks the role and sets it in one.
    std::vector<std::pair<juce::String, juce::var>> asCommands(const juce::String& rigId) const;

private:
    std::map<LevelTarget, std::optional<float>> levels_;
    std::map<LevelTarget, bool> mutes_;                           // keyed by a Part target
    std::map<LevelTarget, int> pans_;                             // keyed by a Part or a Mix target
};

}  // namespace sqlive::app
