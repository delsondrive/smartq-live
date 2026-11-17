#pragma once

#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

using app::Readiness;

// The keys (and Start's pedal) the Show screens name, from the band's controls (app::ControlMap): the sentences, the hints,
// the interrupted banner and the WHEN IT COMES BACK box all say what the band has learned.
struct ShowKeys {
    juce::String startKey;                                         // Start's key ("Space", "Q"); "" when it has none
    bool startPedal = false;                                       // Start has a pedal (said only when it has no key)
    juce::String holdKey;                                          // a key held during a song stops it: Stop's, else Start's
    juce::StringArray songKeys;                                    // the keys of Next song: earlier, later ("←", "→"), those set
    juce::String viewKey;                                          // Main view ⇄ stage view's ("Tab"); "" for none
    juce::StringArray sectionKeys;                                 // the keys of Next section: earlier, later ("↑", "↓"), those set
    juce::String loopKey, cancelKey;                               // "L", "Backspace"; "" for none
    juce::String nextSongNowKey;                                   // the key held to go to the next song now: Go to the next song
                                                                   // now's own, else Next song: later's ("→"); "" for none
    bool numberKeys = false;                                       // Section1 … Section10 still have the keys 1 … 9 and 0
    // The big key and a hint's key for Start: its key, else "Pedal", else "▶" (the top bar's button: nothing else starts).
    juce::String startCap() const;
    // Start in a sentence: its key, else "the Start pedal" ("The Start pedal" at the start of one), else "▶".
    juce::String startWords(bool sentenceStart = false) const;
};
ShowKeys showKeysOf(const model::BandDoc&);

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
    juce::String startKey;                                         // the stage view's big key: ShowKeys::startCap ("Space", "Q",
                                                                   // "Pedal", "▶"); the sentences say ShowKeys::startWords
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
    std::vector<KeyHint> hints;                                    // the key hints along the bottom, with the band's keys
    juce::String setlistName;                                      // the stage view, once the last song has ended
    // The sections of the song the views show (the one playing, else the next): one tile per step of its arrangement, as
    // Session::arrangement() has them, coloured by theme::sectionColour. None for a song with a single step (it shows what
    // stage 1 showed), nor before the next song has loaded. Now is the step playing; Next the step that plays after it (the
    // one chosen, else the one after it; none while the playing one loops); Past the steps before the playing one.
    struct Tile {
        juce::String number, name;                                 // number "3", "3 · NOW", "4 · NEXT"; steps past 10 have none
        juce::Colour colour;
        int bars = 0;
        enum class Kind { Past, Now, Next, Later } kind = Kind::Later;
        bool operator==(const Tile&) const = default;
    };
    std::vector<Tile> tiles;
    // The line above the tiles while a step plays: "Chorus", "6", "bars left" ("bar left"), and ArrangementView::then
    // ("then → Verse 2", "then → Chorus again"; also in a lead-in, with no name and no bars).
    juce::String sectionName, barsLeft, barsLeftLabel, then;
    bool looping = false;
    // The song the main view's map shows (the one playing, else the next): a "Cues" lane first when the song has cues (its
    // automatic and manual ones, as the Song editor's Cues lane has them, the automatic ones marked), then one lane per band
    // device that has a change in it, in the band's order, each change a chip with its model::markerLabel, in the lane's
    // order. A chip's place is across the map's span: for a song with a single step, from the count-in to the end
    // (mapSpanSeconds); with sections, along its steps laid end to end (a section played twice has its chips twice, and
    // those in a section the arrangement leaves out are left out too).
    struct Lane {
        juce::String name;
        juce::Colour colour;
        struct Chip {
            double position01;
            juce::String label;
            bool automatic = false;                                // an automatic cue: drawn dashed, with no colour
            bool operator==(const Chip&) const = default;
        };
        std::vector<Chip> chips;
        bool operator==(const Lane&) const = default;
    };
    std::vector<Lane> lanes;
    bool operator==(const ShowState&) const = default;
};

// A pure function of Session::now(), Session::arrangement(), Session::stageView() (which hints), the setlist, its songs and
// its play mode.
//
// While playing, the fields for the stage view describe the song playing: in a gap between songs, the one about to start.
// Played alone (Edit-mode playback), the label is "NOW PLAYING" and no card plays. Between songs (and after an
// interruption) the "up" fields and the action describe the next song; with nothing next they are empty and endOfSet
// is true. The second line of nextInfo says what happens between the song playing and the next one; between songs
// there is none, and nextInfo has its first line only. The sentences and hints name the band's own keys (app::ControlMap):
// "Press Q to start", "hold S stop"; a hint whose action has no key is left out. The section hints show only for a song
// with sections: the main view's "↑ ↓ next section", "L loop" and "Backspace cancel" while it plays; the stage view's
// "↑ ↓ next section", "L loop this section", "Backspace cancel" and "hold → go to next song now" while it plays, and
// "1–9 start from a section" between songs, while Section1 … Section10 have the keys 1 … 9 and 0.
//
// It runs 30 times a second. What it works out from documents alone is kept, message thread only: the keys, from the
// band's controls, again only when the band's model::BandModel::revision changes; the lanes, when the song's or the
// band's revision or the song's steps change; the tiles' names, colours and bars, when the band's revision or the
// song's steps (their names, types and bars: the song or its arrangement) change. Each tick works out only the tiles'
// kinds, the bars left, `then` and `looping`, from Session::arrangement().
ShowState makeShowState(const app::Session&);
// For tests: how many times makeShowState has worked out the keys, the lanes, and the tiles' names, colours and bars,
// since the program started.
struct ShowStateWork {
    int keys = 0, lanes = 0, tiles = 0;
};
ShowStateWork showStateWork();

// The span the main view's map draws for a song with a single step, in song time (seconds from bar 1):
// model::songExtentSeconds of its model::tempoMapOf, from the count-in (or the audio before bar 1) to the end.
std::pair<double, double> mapSpanSeconds(const model::SongDoc&);

// A song's steps laid end to end, as the main view's map lays out its section blocks, its waveform and its chips: each
// step's section in song time, and where the step starts along them, in seconds.
struct StepSpans {
    struct Span {
        double songStart = 0.0, songEnd = 0.0, at = 0.0;
        bool operator==(const Span&) const = default;
    };
    std::vector<Span> steps;
    double total = 0.0;                                            // the steps' lengths added up
    bool operator==(const StepSpans&) const = default;
};
StepSpans stepSpansOf(const std::vector<app::StepView>&);

}  // namespace sqlive::ui
