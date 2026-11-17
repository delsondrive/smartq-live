#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "engine/MixSetup.h"
#include "engine/PreparedSong.h"
#include "engine/Types.h"

namespace sqlive::engine {

// What one render call plays.
struct RenderParts {
    bool parts = true;        // the song's parts
    bool click = true;        // its click, as its ClickSetup says
    bool advance = true;      // false: every part gain, mix level and the click gain is read as it stands and not stepped. A
                              // jump renders its outgoing tail so, beside the incoming sample, so no glide runs twice as fast
    SamplePos forcedClickFrom = 0, forcedClickTo = 0;   // the song's ticks in [from, to) sound even with its click off,
                                                        // those before its start too (startAtStep's count-off)
    const float* cue = nullptr;     // `frames` samples of mono cue audio, mixed as the mix's cuesRole inside the per-sample loop, through
                                    // the same smoothed mix levels as the parts and the click; nullptr = none
};

// Mixes one song's parts, click and cue voice into the output buses of a mix. Every gain, every mix level, the click gain
// and the cue level glide on a one-pole with a 10 ms time constant, advanced once per sample, so the output never depends
// on how the calls to render are split into blocks. The cue is mixed at the cue level times the song's cuesAutomation at
// each sample (unsmoothed: it has its own straight lines), and never through the envelope: the voice has fades of its own.
// With no song set, render mixes only a cue it is given, at the preview gain. prepare allocates; nothing else does. The
// song and the mix must outlive their use here.
class SongRenderer {
public:
    void prepare(double sampleRate, int maxBlockFrames);
    void setSong(const PreparedSong*) noexcept;       // snaps every part gain to the song's
    void setMix(const MixSetup*) noexcept;            // snaps the first time or when the bus layout changed; otherwise glides
    void setPartGain(int part, float linear) noexcept;
    void setPartMute(int part, bool) noexcept;
    void setClickGain(float linear) noexcept;
    void setCueGain(float linear) noexcept;           // the band's cue level: snapped to the song's cueGain at setSong, glides after
    // The level of a cue rendered with no song set (Engine::previewCue). setSong(nullptr) sets it to the level the song's cue
    // last had, so a voice still fading out as a song ends carries on at that level.
    void setPreviewGain(float linear) noexcept;
    void setSolo(int part) noexcept;                 // -1 = none; a soloed part silences the other parts, never the click
    // true: ignore that part's automation (its level is its fader alone) until setSong; false: follow it again. A part the song
    // doesn't have is ignored.
    void setPartOverride(int part, bool) noexcept;
    // Adds into out; envelope may be null.
    void render(SamplePos songTime, int frames, float* const* out, int numChannels, const float* envelope,
                RenderParts = {}) noexcept;

private:
    struct Smoothed {
        double value = 0.0, target = 0.0;   // double: a float one-pole stalls short of its target, so would never snap
    };
    struct PartView {                       // what render reads of a part, worked out once in setSong
        const float* left = nullptr;
        const float* right = nullptr;       // null = mono
        SamplePos length = 0;
        int role = -1;                      // -1 = not played
    };

    void step(Smoothed&) const noexcept;
    void updatePartTargets() noexcept;
    bool partSilenced(int part) const noexcept { return part_mute_[part] != 0 || (solo_ >= 0 && solo_ != part); }
    void followAutomation(SamplePos at, bool snap) noexcept;
    void countAutomated() noexcept;
    void renderPiece(SamplePos songTime, int frames, int offset, const float* envelope, RenderParts) noexcept;
    void renderCueAlone(int frames, int offset, const float* cue) noexcept;   // no song: the cue at the preview gain
    void stepCells() noexcept;                                                 // every mix cell, one sample on
    void mixCue(float value, int at) noexcept;                                 // into every bus the cue role reaches
    std::size_t firstTickAt(SamplePos) const noexcept;   // the first of song_->ticks at or after it; song_ must be set

    double coefficient_ = 0.0;
    int max_block_ = 0;
    std::vector<float> click_;              // the click of one piece, mono
    std::vector<Smoothed> part_gain_;       // kMaxParts, after mute and solo
    std::vector<float> part_level_;         // kMaxParts, as set, before mute and solo
    std::vector<unsigned char> part_mute_;  // kMaxParts
    std::vector<PartView> part_view_;       // kMaxParts
    std::vector<Smoothed> cells_;           // kMaxRoles × kMaxBuses, row-major by role: the mix level × the bus trim
    Smoothed click_gain_;
    Smoothed cue_gain_;                     // the band's cue level
    float preview_gain_ = 1.0f;             // the cue's level with no song set
    float last_cue_level_ = 1.0f;           // the cue level × automation at the last cue sample mixed with the song

    // Where a part's automation has got to: the index of the last point at or before the song time it expects next (-1: before
    // the first). When the time it is given is not that one (a jump, startAt) it finds its place again by bisection.
    struct AutoCursor {
        std::span<const AutoPoint> points;
        std::ptrdiff_t last = -1;
        SamplePos expected = 0;
        bool placed = false;                // last and expected mean something
        bool held = false;                  // heldGain is the gain of the segment last is in, which is flat
        float heldGain = 1.0f;
        bool overridden = false;
    };

    const PreparedSong* song_ = nullptr;
    int part_count_ = 0;
    int solo_ = -1;
    std::size_t count_in_first_ = 0;        // song->ticks[count_in_first_, count_in_end_) are the count-in's, which sound
    std::size_t count_in_end_ = 0;          // even with the click off
    std::size_t playable_first_ = 0;        // the first of song->ticks at or after its start: those before are a count-off's
    bool parts_moving_ = false;
    std::vector<AutoCursor> part_auto_;     // kMaxParts
    int automated_ = 0;                     // parts that follow automation: it has points, and is not overridden
    bool snap_pending_ = false;             // the next advancing sample puts every automated part on its target

    bool has_mix_ = false;
    int bus_count_ = 0;
    int role_rows_ = 0;                     // rows of cells_ that may be other than 0
    int click_role_ = -1;
    int cues_role_ = -1;
    bool cells_moving_ = false;
    std::array<int, kMaxBuses> bus_first_{};
    std::array<bool, kMaxBuses> bus_stereo_{};

    // The channels of each bus for the current render call; null where the device has no such channel.
    std::array<float*, kMaxBuses> bus_left_{};
    std::array<float*, kMaxBuses> bus_right_{};
};

} // namespace sqlive::engine
