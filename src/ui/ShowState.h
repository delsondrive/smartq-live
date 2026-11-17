#pragma once

#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"

namespace sqlive::ui {

using app::Readiness;

// Everything both Show views draw (the mockup's screens 1 to 3), in their words.
struct ShowState {
    // A setlist card. Done: before the one playing (or, between songs, before the next); Playing: the one playing; Next:
    // the next; Later: the rest. The note says what follows the song ("→ stop", "→ gap 4 s", "→ seamless", "end"); the
    // next card's says whether it can play instead ("✓ Ready", "Loading 62%", "Can't play").
    struct Card {
        enum class Kind { Done, Playing, Next, Later };
        int number = 0;
        juce::String title, length, note;
        Kind kind = Kind::Later;
        double progress = 0.0;                                     // the playing card's, 0..1
        bool noteGood = false, noteBad = false;
        bool operator==(const Card&) const = default;
    };
    std::vector<Card> cards;
    bool playing = false, countIn = false, interrupted = false, endOfSet = false;
    // The stage view, while a song plays (and the place it reached, when interrupted).
    juce::String label, title;                                     // "NOW PLAYING · 3 OF 7", "Hold The Line"
    juce::String bar, beat, bpm, bpmLabel, left;                   // "33", ".1", "128", "BPM · 4/4", "2:38"
    int beatsInBar = 4, beatNow = 0;                               // beatNow 1-based; 0 = none
    double songProgress = 0.0;
    // The next song.
    juce::String nextLabel, nextTitle, nextInfo;                   // "NEXT SONG · ✓ READY", "4 · Long Way Down", "132 BPM · Key A\nafter a 4 s gap"
    // The stage view, between songs.
    juce::String upLabel, upTitle, upBpm, upBpmLabel, upKey, upLength;   // "UP NEXT · 4 OF 7", …, "BPM · 4/4" (its first metre)
                                                                   // upKey "" when the song has no key
    juce::String action, actionDetail, pill;                       // "Press Space to start", "2 bars of count-in, then bar 1.", "✓ READY"
                                                                   // (the detail adds "The pedalboards switch to this song's first
                                                                   // patches when you press." when the song has a pedal change)
    juce::String startKey;                                         // what starts a song, as the sentences name it: Start's key
                                                                   // ("Space", "Q"), else its pedal ("CC 64, ch 10"), else "▶" (the
                                                                   // top bar's button: nothing else starts)
    Readiness readiness = Readiness::None;
    double loadProgress = 0.0;
    struct After {
        enum class Kind { Played, Next, Later };
        juce::String text;
        Kind kind = Kind::Later;
        bool operator==(const After&) const = default;
    };
    std::vector<After> after;                                      // between songs only; Played is NowPlaying::lastPlayedIndex
                                                                   // (none before the first song ends or is stopped)
    juce::StringArray hints;                                       // the key hints along the bottom, with the band's keys
    juce::String setlistName;                                      // the stage view, once the last song has ended
    // The song the main view's map shows (the one playing, else the next): one lane per band device that has a change in
    // it, in the band's order, each change a chip with its model::markerLabel at its place across the map's span (from
    // the count-in to the end, model::songExtentSeconds), in the lane's order.
    struct Lane {
        juce::String name;
        juce::Colour colour;
        struct Chip {
            double position01;
            juce::String label;
            bool operator==(const Chip&) const = default;
        };
        std::vector<Chip> chips;
        bool operator==(const Lane&) const = default;
    };
    std::vector<Lane> lanes;
    bool operator==(const ShowState&) const = default;
};

// A pure function of Session::now(), Session::stageView() (which hints), the setlist, its songs and its play mode.
//
// While playing, the fields for the stage view describe the song playing: in a gap between songs, the one about to start.
// Played alone (Edit-mode playback), the label is "NOW PLAYING" and no card plays. Between songs (and after an
// interruption) the "up" fields and the action describe the next song; with nothing next they are empty and endOfSet
// is true. The second line of nextInfo says what happens between the song playing and the next one; between songs
// there is none, and nextInfo has its first line only. The sentences and hints name the band's own keys (app::ControlMap):
// "Press Q to start", "hold S stop".
ShowState makeShowState(const app::Session&);

// The span the main view's map draws for a song, in song time (seconds from bar 1): model::songExtentSeconds of its
// model::tempoMapOf, from the count-in (or the audio before bar 1) to the end.
std::pair<double, double> mapSpanSeconds(const model::SongDoc&);

}  // namespace sqlive::ui
