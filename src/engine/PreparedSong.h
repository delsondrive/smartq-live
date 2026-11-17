#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "engine/Click.h"
#include "engine/Midi.h"
#include "engine/TempoMap.h"
#include "engine/Types.h"

namespace sqlive::engine {

inline constexpr int kMaxParts = 128;   // parts of a song beyond this are not played

struct PartAudio {
    std::vector<float> left, right;   // right empty = mono; otherwise the same length
};

// A level that changes by itself: at song time `at` the level is `db`. A list of them is sorted by `at` (a tie keeps its written
// order: a mute is two points at the same time), and the level moves in a straight line in dB from one point to the next, held
// before the first and after the last.
struct AutoPoint {
    SamplePos at = 0;
    float db = 0.0f;
};

// The linear gain the points give at song time `at`: 1 when there are none; a straight line in dB between points, flat
// outside them; -60 dB and below is 0. Where points share a time the last of them counts from that time on. Precondition:
// the points are sorted by `at`, ties in written order: the caller supplies them so (Task 8's buildPreparedSong will sort
// them, stably); the audio thread never sorts.
// Searches by bisection, allocates nothing. std::pow runs only inside a segment whose two points differ in dB; a held level
// is worked out once (the renderer does so once per segment, not per sample).
float automationGain(std::span<const AutoPoint>, SamplePos at) noexcept;

// The same, given where `at` falls: `last` is the index of the last point with a time <= at, or -1 before the first.
float automationGainIn(std::span<const AutoPoint>, std::ptrdiff_t last, SamplePos at) noexcept;
// Whether the gain between points `last` and `last + 1` (see above) is one held value: nothing differs in dB.
bool automationFlat(std::span<const AutoPoint>, std::ptrdiff_t last) noexcept;

struct PreparedPart {
    std::shared_ptr<const PartAudio> audio;
    int role = 0;
    float gain = 1.0f;
    bool mute = false;
    std::string partId;               // the part document this came from, so the control thread can find it again
    std::vector<AutoPoint> automation;   // this part's level over song time, on top of its fader; empty = none
};

struct ClickSetup {
    bool enabled = true;              // off still clicks through the count-in, but not through pre-roll before it
    float gain = 1.0f;
    Subdivision subdivision = Subdivision::None;
    int sound = 0;
    bool accentDownbeat = true;
};

struct Section { SamplePos start = 0, end = 0; };                 // song time; end is one past the last sample

struct CueEvent { SamplePos at = 0; std::shared_ptr<const PartAudio> clip; };   // a manual cue, spoken at `at`

// When a chosen step (or a loop turned on) takes effect: at the end of the playing section, or at the next bar line.
enum class JumpTiming : std::uint8_t { EndOfSection, NextBar };

// Everything the audio thread needs to play one song, built on the control thread and never changed after.
struct PreparedSong {
    std::string id;
    double sampleRate = 48000.0;
    TempoMap tempo;
    SamplePos audioStartOffset = 0;    // where bar 1 beat 1 falls in every part's audio
    SamplePos start = 0;               // song time playback begins at, never above 0
    SamplePos bodyStart = 0;           // where it begins when the count-in is skipped: -audioStartOffset
    SamplePos countInStart = 0;        // where the count-in begins: minus its length, 0 when there is none
    SamplePos end = 0;                 // song time one past the last sample
    std::vector<PreparedPart> parts;
    ClickSetup click;
    std::vector<ClickTick> ticks;      // every tick in [start, end), from the bar before bar 1 if that is earlier (for a
                                       // count-off only); those in [countInStart, 0) sound even with the click off
    ClickSound clickSound;
    std::vector<AutoPoint> clickAutomation, cuesAutomation;   // the click's level and the cue voice's, on top of theirs
    // MIDI. A device is named by its engine device number: its place in the band when the song was prepared.
    std::vector<MidiEvent> midi;       // sorted by `at`; events at the same time keep their order
    std::vector<std::uint16_t> clockDevices, startStopDevices;
    std::vector<SamplePos> clockTicks; // 24 per quarter over [countOffFloor, end); only when clockDevices isn't empty
    std::vector<std::string> midiDeviceIds;   // the band's device id for each engine device number, as when prepared
    // Sections and the arrangement: the song plays the sections its steps name, in step order, and ends when the last
    // step does. `end` stays the audio's end; the sections tile [0, end).
    std::vector<Section> sections;           // tile [0, end); never empty once finishSections has run
    std::vector<std::uint16_t> arrangement;  // indices into sections; never empty once finishSections has run
    JumpTiming jumpTiming = JumpTiming::EndOfSection;
    std::vector<SamplePos> barStarts;        // every bar start in [0, end)
    // The cue voice. While a step plays and song time reaches announceAt of its section, the section of the step that
    // will play next is announced with its sectionCue; manual cues are said where they are placed. Empty: no cues.
    std::vector<std::shared_ptr<const PartAudio>> sectionCue;     // one per section: the clip that announces it; nullptr = none
    std::vector<SamplePos> announceAt;                            // one per section: when, during that section, to announce what follows
    std::vector<CueEvent> cues;                                   // sorted by `at`
    float cueGain = 1.0f;                                         // the band's cue level when the song was prepared
};

// Where a count-off may begin: the start, or the bar before bar 1 when that is earlier. Click ticks and MIDI clock ticks are
// built from here, so a step at bar 1 still has a bar of both for startAtStep's count-off; ordinary playback never
// reaches before `start`.
SamplePos countOffFloor(const TempoMap&, double sampleRate, SamplePos start);

// explicitEnd below 0 means "where the longest part ends". Runs finishSections on what it makes.
PreparedSong prepareSong(std::string id, TempoMap, int countInBars, SamplePos audioStartOffset,
                         std::vector<PreparedPart>, ClickSetup, double sampleRate, SamplePos explicitEnd = -1);

// Fills in whatever is missing: one section for the whole song, [0, end), when there are none; every section in order
// when the arrangement is empty (or names no section the song has: steps out of range are removed first); the bar
// starts, from the tempo map; and, unless they have one entry per section, sectionCue (all nullptr) and announceAt (each
// section's end, which a step never reaches), so a song without cues announces nothing.
void finishSections(PreparedSong&);

} // namespace sqlive::engine
