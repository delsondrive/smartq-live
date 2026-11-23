// Session's solos, kept here so Session.cpp doesn't grow. A solo is an audition at home or at soundcheck, never part of the
// band: Edit mode only, kept per song in the session (a set of that song's part ids), never saved, and cleared on entering
// Show mode (enterShow) and when the band closes, with the session. It belongs to the song it was set on: soloing a part of
// a song that isn't playing changes nothing heard until that song plays, and a forgotten solo never carries into another
// song. The engine gets a song's set by part index in what it plays (Prepared::partIndex): with the song's cue, so it is
// heard from the song's first sample, and again whenever it changes while the song is cued or playing. A part soloed that
// the engine doesn't play (its audio didn't load) is still a solo: the others go silent, as the mixer shows them. What the
// engine took is kept by serial, and each tick sends again what differs, so a command its full queue dropped (an audio
// thread stalled) is never the last word; entering Show mode, and each song's start, send the right set whatever was taken.

#include <algorithm>
#include <bitset>
#include <map>
#include <memory>

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

Session::SoloSent Session::wantedSolo(const Prepared& prepared) const {
    SoloSent wanted;
    if (mode_ != Mode::Edit) return wanted;                      // a show has no solo
    const auto parts = soloedParts(prepared.doc.id);
    wanted.on = !parts.isEmpty();                                // a part it doesn't play counts: the others go silent
    for (const auto& partId : parts)
        if (const auto index = prepared.partIndex.find(partId);
            index != prepared.partIndex.end() && index->second >= 0 && index->second < engine::kMaxParts)
            wanted.parts.set(static_cast<std::size_t>(index->second));
    return wanted;
}

bool Session::sendSolo(std::uint64_t serial, const Prepared& prepared) {
    if (serial == 0) return true;
    const auto wanted = wantedSolo(prepared);
    if (!engine_.setSoloSet(serial, wanted.parts, wanted.on)) return false;   // the next tick's assertSolos sends it again
    solo_sent_[serial] = wanted;
    return true;
}

void Session::sendSolos(const juce::String& songId) {
    if (playing_.active() && playing_.prepared != nullptr && playing_.prepared->doc.id == songId)
        sendSolo(playing_.serial, *playing_.prepared);
    if (cue_.serial != 0 && cue_.prepared != nullptr && cue_.prepared->doc.id == songId) sendSolo(cue_.serial, *cue_.prepared);
}

void Session::assertSolos(bool force) {
    std::erase_if(solo_sent_, [this](const auto& sent) { return sent.first != playing_.serial && sent.first != cue_.serial; });
    const auto assertOne = [this](std::uint64_t serial, const std::shared_ptr<const Prepared>& prepared, bool always) {
        if (serial == 0 || prepared == nullptr) return true;
        const auto sent = solo_sent_.find(serial);
        if (!always && sent != solo_sent_.end() && sent->second == wantedSolo(*prepared)) return true;
        return sendSolo(serial, *prepared);
    };
    const bool started = playing_.serial != solo_started_;       // a song started since: its solo once more, whatever was taken
    if (assertOne(playing_.serial, playing_.prepared, force || started)) solo_started_ = playing_.serial;
    assertOne(cue_.serial, cue_.prepared, force);
}

}  // namespace sqlive::app
