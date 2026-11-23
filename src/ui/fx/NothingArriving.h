#pragma once

#include "ui/fx/ChainHost.h"
#include "ui/fx/AudioIdle.h"

namespace smartq {

// Whether to say that the sidechain's bus is on and nothing is arriving on it: a module or band that listens to the sidechain, the
// bus on in the host, the track playing, and for about 2 seconds nothing heard on the bus (which the processor itself only says after
// a second of none). The host is probably set to send no track to it. Message thread, a step at a time.
//
// It does not flicker. A gap in the track shorter than a second leaves the count where it was, so a note that is up stays up and one on
// the way keeps coming; a longer one, a stopped transport, a bus turned off or a choice of the track itself starts it over. Sound on the
// bus ends it at once.
class NothingArriving {
public:
    explicit NothingArriving(unsigned blocksProcessed) noexcept : idle_(blocksProcessed) {}

    // `listening` is whether what is shown listens to the sidechain.
    bool update(const ChainHost& processor, bool listening, double dtSeconds) noexcept {
        const bool running = idle_.update(processor.audioBlocksProcessed(), dtSeconds);
        const bool trackIsPlaying = running && processor.inputPeak() > kAudible;

        if (!listening || !running || !processor.sidechainRouted() || processor.sidechainReceiving()) {
            missingSeconds_ = quietSeconds_ = 0.0;
            return false;
        }

        if (trackIsPlaying) {
            quietSeconds_ = 0.0;
            missingSeconds_ += dtSeconds;
        } else if ((quietSeconds_ += dtSeconds) >= kQuietSeconds) {
            missingSeconds_ = 0.0;
        }
        return missingSeconds_ >= kShowSeconds;
    }

private:
    static constexpr double kShowSeconds = 2.0;    // of the track playing with nothing on the bus
    static constexpr double kQuietSeconds = 1.0;   // of the track silent, which counts as it having stopped
    static constexpr float kAudible = 3.1622777e-5f;  // -90 dBFS, as everywhere else that audio is taken to be there

    AudioIdle idle_;
    double missingSeconds_ = 0.0, quietSeconds_ = 0.0;
};

}  // namespace smartq
