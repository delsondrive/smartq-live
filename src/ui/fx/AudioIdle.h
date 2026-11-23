#pragma once

namespace smartq {

// Whether the audio thread is running, told from a count of the blocks it has played. What it last measured means nothing
// when it isn't, as the host stops calling processBlock and leaves those values as they were. Message thread.
class AudioIdle {
public:
    // Audio doesn't count as running until the count moves on from this.
    explicit AudioIdle(unsigned blocksProcessed) noexcept : seenBlocks_(blocksProcessed) {}

    // True while audio has run in the last 0.4 s.
    bool update(unsigned blocksProcessed, double dtSeconds) noexcept {
        if (blocksProcessed != seenBlocks_) {
            seenBlocks_ = blocksProcessed;
            secondsSinceBlock_ = 0.0;
        } else {
            secondsSinceBlock_ += dtSeconds;
        }
        return secondsSinceBlock_ < kIdleSeconds;
    }

private:
    static constexpr double kIdleSeconds = 0.4;  // without a block for this long, no audio is taken to be running

    unsigned seenBlocks_;
    double secondsSinceBlock_ = 1.0e9;
};

}  // namespace smartq
