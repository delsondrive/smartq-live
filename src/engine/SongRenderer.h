#pragma once

#include <array>
#include <bitset>
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
    // startAtStep's count-off (CountOff): over [countOffFrom, countOffTo) the song's own ticks are left out and these sound
    // instead, whatever the song's click setting, at the click's own level (no automation), the downbeat accented.
    SamplePos countOffFrom = 0, countOffTo = 0;
    std::span<const ClickTick> countOffTicks;
    const float* cue = nullptr;     // `frames` samples of mono cue audio, mixed as the mix's cuesRole inside the per-sample loop, through
                                    // the same smoothed mix levels as the parts and the click; nullptr = none
    // false: the click is not faded by render's envelope but by clickEnvelope (`frames` gains; nullptr = none). A song
    // fading in at a crossfade keeps its click at full level: the drummer needs the new tempo at once.
    bool clickFollowsEnvelope = true;
    const float* clickEnvelope = nullptr;
};

// Mixes one song's parts, click and cue voice into the output buses of a mix. Every gain, every mix level, the click gain
// and the cue level glide on a one-pole with a 10 ms time constant, advanced once per sample, so the output never depends
// on how the calls to render are split into blocks. The cue is mixed at the cue level times the song's cuesAutomation at
// each sample (unsmoothed: it has its own straight lines), and never through the envelope: the voice has fades of its own.
// With no song set, render mixes only a cue it is given, at the preview gain. prepare allocates; nothing else does. The
// song and the mix must outlive their use here.
//
// Pan: on a stereo bus each part is placed by balanceGains of the pan that applies there, the bus's own pan for the part's
// role when the mix gives it one (MixSetup::panOwnAt), else the song's pan of the part; the click and the cues by their
// role's own pan on the bus (centre without one). Each part's left and right gain on each bus, and each role's, glides on
// the same one-pole as the levels (a drag doesn't click), snapping where the gains snap: at setSong for the parts, and at
// a setMix that snaps. A mono bus ignores pan. A centre pan multiplies by exactly 1, so a song with every pan at centre
// renders bit for bit as it did before pan existed.
//
// Solo: a set of parts; while it names any part the song has, every part not in it is silent (glides out, as a mute). The
// click and the cue voice are never silenced by it.
class SongRenderer {
public:
    void prepare(double sampleRate, int maxBlockFrames);
    void setSong(const PreparedSong*) noexcept;       // snaps every part gain and pan to the song's; clears the solo
    void setMix(const MixSetup*) noexcept;            // snaps the first time or when the bus layout changed; otherwise glides
    void setPartGain(int part, float linear) noexcept;
    void setPartMute(int part, bool) noexcept;
    void setPartPan(int part, int pan) noexcept;      // the song's pan of the part, -100 to 100 (limited); glides
    void setClickGain(float linear) noexcept;
    void setCueGain(float linear) noexcept;           // the band's cue level: snapped to the song's cueGain at setSong, glides after
    // The level of a cue rendered with no song set (Engine::previewCue). setSong(nullptr) sets it to the level the song's cue
    // last had, so a voice still fading out as a song ends carries on at that level.
    void setPreviewGain(float linear) noexcept;
    // The soloed parts (empty = none; parts the song doesn't have are left out, so a set of only those is none). Set before
    // the song's first sample (right after setSong) it is heard from that sample; afterwards the parts glide.
    void setSoloSet(const std::bitset<kMaxParts>&) noexcept;
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

    struct Balance {                        // a pan's left and right gains on one bus, gliding
        Smoothed left{1.0, 1.0}, right{1.0, 1.0};
    };

    void step(Smoothed&) const noexcept;
    void updatePartTargets() noexcept;
    bool partSilenced(int part) const noexcept {
        return part_mute_[part] != 0 || (solo_any_ && !solo_[static_cast<std::size_t>(part)]);
    }
    void updatePartBalance(int part, bool snap) noexcept;   // its balance on each bus, from its pan and the buses' own pans
    void stepBalances() noexcept;                           // every part's and role's balance, one sample on
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
    std::vector<int> part_pan_;             // kMaxParts: the song's pan of each part
    std::vector<Balance> part_bal_;         // kMaxParts × kMaxBuses, row-major by part: the balance each part is mixed at
    std::vector<Balance> cell_bal_;         // kMaxRoles × kMaxBuses: each role's own pan (centre without one), for the click
                                            // and the cues
    std::vector<int> own_pan_;              // kMaxRoles × kMaxBuses: the mix's own pan of each role on each bus
    std::vector<unsigned char> own_;        // kMaxRoles × kMaxBuses: 1 where that own pan applies
    bool bal_moving_ = false;               // some balance hasn't reached its target
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
    std::bitset<kMaxParts> solo_;           // the soloed parts, only of those the song has
    bool solo_any_ = false;
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
