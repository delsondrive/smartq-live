#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <utility>

#include "engine/PreparedSong.h"
#include "engine/Types.h"

namespace sqlive::engine {

// The cue voice: speaks one clip at a time, mono. Two voices: the clip speaking, and the one it cut, fading out under it.
// A new clip fades in over 1 ms; the one it cuts fades out in a straight line over 5 ms from the level it had. A stereo
// clip is spoken as the mean of its sides.
//
// Audio thread only, after prepare (which allocates nothing either, and frees what it held: call it only while the audio
// thread isn't running). It never allocates or frees: a clip it has finished with waits in a small fixed store until
// collect hands it on (the engine's retire queue). Whoever calls say must still hold the clip it passes, so that a say
// refused (the store full) never drops the last reference.
class CuePlayer {
public:
    static constexpr int kVoices = 2;

    void prepare(double sampleRate, int maxBlockFrames);
    void say(std::shared_ptr<const PartAudio> clip) noexcept;    // cuts whatever is speaking with a 5 ms fade; null: silence()
    void silence() noexcept;                                      // the same fade, nothing new
    // Adds into monoOut; returns how many of its first frames either voice sounded in (0: silent throughout), so a caller
    // mixing the voice alone can stop exactly where it fell silent, whatever the block.
    int render(int frames, float* monoOut) noexcept;

    bool active() const noexcept;                                 // either voice still sounds
    const PartAudio* speaking() const noexcept;                   // the clip the speaking voice is saying; null when none

    // Hands finished clips to retire, at most `budget` of them, so the audio thread never frees one; returns how many it handed.
    template <typename Retire> int collect(Retire&& retire, int budget) noexcept {
        releaseSilent(speaking_);
        releaseSilent(fading_);
        int handed = 0;
        while (handed < budget && handed < finished_count_) {
            retire(std::move(finished_[static_cast<std::size_t>(handed)]));
            ++handed;
        }
        for (int i = handed; i < finished_count_; ++i)   // into slots already emptied, so nothing is freed
            finished_[static_cast<std::size_t>(i - handed)] = std::move(finished_[static_cast<std::size_t>(i)]);
        finished_count_ -= handed;
        return handed;
    }

private:
    static constexpr int kFinishedCapacity = 16;

    struct Voice {
        std::shared_ptr<const PartAudio> clip;   // null: the voice is free
        const float* left = nullptr;
        const float* right = nullptr;            // null = mono
        SamplePos length = 0;
        SamplePos pos = 0;                       // the next sample of the clip
        bool fadingOut = false;
        double from = 1.0;                       // fading out: the level it had when cut
        std::int64_t fadeLeft = 0;               // fading out: samples of the fade still to come
    };

    static bool sounds(const Voice&) noexcept;
    double gainOf(const Voice&) const noexcept;  // the level of the voice's next sample, before it is played
    bool release(Voice&) noexcept;               // its clip into finished_; false (and nothing changed) when that is full
    void releaseSilent(Voice&) noexcept;         // the same, only for a voice holding a clip that no longer sounds
    int renderVoice(Voice&, int frames, float* monoOut) noexcept;   // returns the frames it sounded in

    std::int64_t fade_in_ = 0;                   // 1 ms
    std::int64_t fade_out_ = 0;                  // 5 ms
    Voice speaking_, fading_;
    std::array<std::shared_ptr<const PartAudio>, kFinishedCapacity> finished_;
    int finished_count_ = 0;
};

} // namespace sqlive::engine
