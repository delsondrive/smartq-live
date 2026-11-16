#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "engine/MixSetup.h"
#include "engine/PreparedSong.h"
#include "engine/Types.h"

namespace sqlive::engine {

// Mixes one song's parts and click into the output buses of a mix. Every gain, every mix level and the click gain glide
// on a one-pole with a 10 ms time constant, advanced once per sample, so the output never depends on how the calls to
// render are split into blocks. prepare allocates; nothing else does. The song and the mix must outlive their use here.
class SongRenderer {
public:
    static constexpr int kMaxParts = 128;          // parts beyond this are not played

    void prepare(double sampleRate, int maxBlockFrames);
    void setSong(const PreparedSong*) noexcept;       // snaps every part gain to the song's
    void setMix(const MixSetup*) noexcept;            // snaps the first time or when the bus layout changed; otherwise glides
    void setPartGain(int part, float linear) noexcept;
    void setPartMute(int part, bool) noexcept;
    void setClickGain(float linear) noexcept;
    void setSolo(int part) noexcept;                  // -1 = none; a soloed part silences the other parts, never the click
    void render(SamplePos songTime, int frames, float* const* out, int numChannels, const float* envelope) noexcept;  // adds; envelope may be null

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
    void renderPiece(SamplePos songTime, int frames, int offset, const float* envelope) noexcept;

    double coefficient_ = 0.0;
    int max_block_ = 0;
    std::vector<float> click_;              // the click of one piece, mono
    std::vector<Smoothed> part_gain_;       // kMaxParts, after mute and solo
    std::vector<float> part_level_;         // kMaxParts, as set, before mute and solo
    std::vector<unsigned char> part_mute_;  // kMaxParts
    std::vector<PartView> part_view_;       // kMaxParts
    std::vector<Smoothed> cells_;           // kMaxRoles × kMaxBuses, row-major by role: the mix level × the bus trim
    Smoothed click_gain_;

    const PreparedSong* song_ = nullptr;
    int part_count_ = 0;
    int solo_ = -1;
    std::size_t count_in_first_ = 0;        // song->ticks[count_in_first_, count_in_end_) are the count-in's, which sound
    std::size_t count_in_end_ = 0;          // even with the click off
    bool parts_moving_ = false;

    bool has_mix_ = false;
    int bus_count_ = 0;
    int role_rows_ = 0;                     // rows of cells_ that may be other than 0
    int click_role_ = -1;
    bool cells_moving_ = false;
    std::array<int, kMaxBuses> bus_first_{};
    std::array<bool, kMaxBuses> bus_stereo_{};

    // The channels of each bus for the current render call; null where the device has no such channel.
    std::array<float*, kMaxBuses> bus_left_{};
    std::array<float*, kMaxBuses> bus_right_{};
};

} // namespace sqlive::engine
