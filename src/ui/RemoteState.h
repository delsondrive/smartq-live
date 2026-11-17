#pragma once

#include <juce_core/juce_core.h>

#include "app/Session.h"

namespace sqlive::ui {

// What a phone on the remote is shown: the show as the laptop's Show views have it (makeShowState), with the numbers from
// Session::now(), as one line of JSON, the same for every phone let in. Message thread only. It says nothing about who
// asks: a phone still waiting to be let in is given none of it (RemoteServer's to enforce).
//
//   {"show": true, "playing": true, "countIn": false, "interrupted": false, "canGoOn": true,
//    "song": {"title": "Hold The Line", "number": 3, "of": 7},
//    "bar": 33, "beat": 1, "beatsInBar": 4, "bpm": "128", "left": "2:38", "progress": 0.46,
//    "section": "Chorus", "barsLeft": 6, "then": "then → Verse 2", "looping": false,
//    "tiles": [{"name": "Chorus", "colour": "ffc163", "kind": "now", "choosable": true}, …],
//    "next": {"title": "Long Way Down", "number": 4, "ready": "ready"},
//    "setlist": [{"title": "Intro", "note": "1:30", "kind": "done"}, {"title": "Hold The Line", "note": "playing", "kind": "playing"}, …]}
//
// - show: the laptop is in Show mode. canGoOn: NowPlaying::canGoOn, so the page knows a hold toward the next song can act.
// - song: the one playing (in a gap, the one about to start), null with nothing playing; number is its place in the
//   setlist (null for a song played on its own), of the setlist's length.
// - bar, beat, beatsInBar, bpm: NowPlaying's; with nothing playing, the next song at bar 1 (after an interruption, the bar
//   it reached). left: the time left, or the next song's length with nothing playing. progress: 0..1 along the song.
// - section, barsLeft, then, looping: the line above the tiles ("" and 0 when no step plays).
// - tiles: one per step of the song shown (none for a song with a single step); kind past, now, next or later; colour
//   rrggbb (theme::sectionColour); choosable: steps 1 to 10, only while a song plays (stopped, a phone tap never starts one).
// - next: the next song (null after the last); ready is "ready", "loading", "failed" or "none".
// - setlist: every entry; kind done, playing, next or later; note "playing", "next", else the entry's length.
juce::String makeRemoteStateJson(const app::Session&);

}  // namespace sqlive::ui
