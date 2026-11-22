// Session's solos, kept here so Session.cpp doesn't grow. A solo is an audition at home or at soundcheck, never part of the
// band: Edit mode only, kept per song in the session (a set of that song's part ids), never saved, and cleared on entering
// Show mode (enterShow) and when the band closes, with the session. It belongs to the song it was set on: soloing a part of
// a song that isn't playing changes nothing heard until that song plays, and a forgotten solo never carries into another
// song. The engine gets a song's set by part index in what it plays (Prepared::partIndex): with the song's cue, so it is
// heard from the song's first sample, and again whenever it changes while the song is cued or playing.

#include <algorithm>
#include <bitset>

#include "app/Session.h"

namespace sqlive::app {

void Session::setSolo(const juce::String& songId, const juce::String& partId, bool on) {
    if (mode_ == Mode::Show) return;                             // no solo in a show: a stray tap must never silence the band
    const auto* song = model_.song(songId);
    if (song == nullptr) return;
    const bool hasPart = std::any_of(song->parts.begin(), song->parts.end(), [&](const model::PartDoc& p) { return p.id == partId; });
    if (!hasPart) return;
    auto& set = solos_[songId];
    if (on == set.contains(partId)) return;
    if (on) set.add(partId);
    else set.removeString(partId);
    if (set.isEmpty()) solos_.erase(songId);
    sendSolos(songId);
    changed();
}

bool Session::soloed(const juce::String& songId, const juce::String& partId) const {
    const auto found = solos_.find(songId);
    return found != solos_.end() && found->second.contains(partId);
}

juce::StringArray Session::soloedParts(const juce::String& songId) const {
    juce::StringArray parts;
    const auto found = solos_.find(songId);
    const auto* song = model_.song(songId);
    if (found == solos_.end() || song == nullptr) return parts;
    for (const auto& part : song->parts)
        if (found->second.contains(part.id)) parts.add(part.id);
    return parts;
}

void Session::sendSolo(std::uint64_t serial, const Prepared& prepared) {
    if (serial == 0) return;
    std::bitset<engine::kMaxParts> set;
    if (mode_ == Mode::Edit)
        if (const auto found = solos_.find(prepared.doc.id); found != solos_.end())
            for (const auto& partId : found->second)
                if (const auto index = prepared.partIndex.find(partId);
                    index != prepared.partIndex.end() && index->second >= 0 && index->second < engine::kMaxParts)
                    set.set(static_cast<std::size_t>(index->second));
    engine_.setSoloSet(serial, set);
}

void Session::sendSolos(const juce::String& songId) {
    if (playing_.active() && playing_.prepared != nullptr && playing_.prepared->doc.id == songId)
        sendSolo(playing_.serial, *playing_.prepared);
    if (cue_.serial != 0 && cue_.prepared != nullptr && cue_.prepared->doc.id == songId) sendSolo(cue_.serial, *cue_.prepared);
}

}  // namespace sqlive::app
