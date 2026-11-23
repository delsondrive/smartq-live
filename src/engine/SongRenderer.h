#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "engine/Meters.h"
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
//
// Meters (Meters.h): given a BlockMeters, render raises it sample by sample as it mixes, reading what it mixes and never
// writing the output: each part's peak (its gain, the envelope and the balance of its song pan, which for the meter is
// taken as it is set, not gliding), the click's and the cues', and, on the buses the BlockMeters watches, each role's sum
// through the bus's cell, own pan or the parts' song pans. So the output is the same with or without meters.
class SongRenderer {
public:
    void prepare(double sampleRate, int maxBlockFrames);
    // Snaps every part gain and pan to the song's; clears the solo, and the part meters when it measures them.
    void setSong(const PreparedSong*) noexcept;
    void setMix(const MixSetup*) noexcept;            // snaps the first time or when the bus layout changed; otherwise glides
    void setPartGain(int part, float linear) noexcept;
    void setPartMute(int part, bool) noexcept;
    void setPartPan(int part, int pan) noexcept;      // the song's pan of the part, -100 to 100 (limited); glides
    void setClickGain(float linear) noexcept;
    void setCueGain(float linear) noexcept;           // the band's cue level: snapped to the song's cueGain at setSong, glides after
    // The level of a cue rendered with no song set (Engine::previewCue). setSong(nullptr) sets it to the level the song's cue
    // last had, so a voice still fading out as a song ends carries on at that level.
    void setPreviewGain(float linear) noexcept;
    // The soloed parts, and whether a solo is on: while it is, every part not in the set is silent, so a set of only parts
    // the song doesn't have (or an empty one, on) silences them all. The first form is on whenever the set isn't empty. Set
    // before the song's first sample (right after setSong) it is heard from that sample; afterwards the parts glide. The
    // same solo again changes nothing.
    void setSoloSet(const std::bitset<kMaxParts>& parts) noexcept { setSoloSet(parts, parts.any()); }
    void setSoloSet(const std::bitset<kMaxParts>&, bool on) noexcept;
    // true: ignore that part's automation (its level is its fader alone) until setSong; false: follow it again. A part the song
    // doesn't have is ignored.
    void setPartOverride(int part, bool) noexcept;
    // Where render raises its peaks (null: nowhere; kept through prepare). parts: whether this renderer's parts are measured
    // (the playing song's are; a song fading out on the engine's second renderer adds only to the click, cues and roles).
    void setMeters(BlockMeters*, bool parts = true) noexcept;
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
        return part_mute_[part] != 0 || (solo_on_ && !solo_[static_cast<std::size_t>(part)]);
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

    // One sample of a role, for its meters on the watched buses: its parts and centred signals before the pan (left,
    // right), after their song pans (panLeft, panRight), and as a mono bus takes them (mono). Each scaled by the part's gain
    // and the envelope; the bus's cell (level × trim) and own pan come at closeMeterSample.
    struct RoleSum {
        float left = 0.0f, right = 0.0f, panLeft = 0.0f, panRight = 0.0f, mono = 0.0f;
    };
    void listMeterRoles() noexcept;   // the roles of the song's parts, the click and the cues, each once
    void meterPart(int part, int role, float left, float right, float mono, float gain) noexcept;
    void meterCentred(int role, float value, float& peak) noexcept;   // the click or a cue sample
    void closeMeterSample() noexcept;   // each listed role's sample onto its meter on each watched bus, then cleared

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
    bool solo_on_ = false;
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

    BlockMeters* meters_ = nullptr;
    bool meter_parts_ = true;
    std::uint32_t meter_buses_ = 0;                        // the current render call's watched buses, of those the mix has
    std::vector<std::pair<float, float>> song_bal_;        // kMaxParts: each part's song pan as gains, for its meter
    std::vector<RoleSum> role_sums_;                       // kMaxRoles
    std::array<int, kMaxRoles> meter_roles_{};             // the first meter_role_count_ are the roles listMeterRoles found
    int meter_role_count_ = 0;
};

} // namespace sqlive::engine
