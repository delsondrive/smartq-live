#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include "engine/Engine.h"
#include "engine/Meters.h"

namespace sqlive::app {

class MeterReader;

// The engine's one meter taker (Engine::takePeak), which hands every peak it takes on to every MeterReader. The engine's
// peaks can be taken only once; so when any reader asks for a meter, the hub takes that meter's peak since the last take
// and raises it in every reader's own copy, then gives the asking reader its own copy and clears it. Each reader therefore
// sees every peak since it last asked for that meter, whatever the others do, and the audio thread never knows how many
// readers there are. A reader joining gets none of the peaks before it: the hub first takes every meter into the readers
// already there. When the playing song changes (Engine::partMetersSerial, looked at before every take), every reader's
// copies of the part peaks go, so one song's part peaks are never read as another's. The session owns one
// (Session::meters); it must outlive its readers. Any thread (a mutex, never touched by the audio thread).
class MeterHub {
public:
    explicit MeterHub(engine::Engine&);
    ~MeterHub();
    MeterHub(const MeterHub&) = delete;
    MeterHub& operator=(const MeterHub&) = delete;

private:
    friend class MeterReader;
    void join(MeterReader&);
    void leave(MeterReader&);
    engine::MeterPeak read(MeterReader&, int slot);   // slot in range
    void followSong();                                // under the lock
    void take(int slot);                              // under the lock

    engine::Engine& engine_;
    std::mutex mutex_;
    std::vector<MeterReader*> readers_;
    std::uint64_t song_ = 0;                          // the serial the readers' part copies belong to
};

// One a view (an output's meter in the routing grid, a mixer view): the live meters as that view sees them. Each call is
// the largest magnitude, left and right, since this reader last asked for that meter (from when the reader was made, the
// first time); {0, 0} for a meter out of range. Two readers never affect each other.
// - part: a part of the playing song, after its fader, mute, solo, automation and song pan, before any output; only that
//   song's peaks (a song change drops what was not yet read). A view showing another song must not show these.
// - click, cues: their signal at their level, before any output, centred (left = right).
// - roleOnBus: what a role puts into an output's bus (its mix level, the trim, the pan that applies, over its parts).
//   Measured only while someone asks (Engine::watchBus): the first ask starts it, from the next audio block.
// - bus: what the bus's device channels got (a mono bus: the same on both sides).
// Allocates its own copy of every meter when made (a few tens of kB); nothing after.
class MeterReader {
public:
    struct Peak {
        float left = 0.0f, right = 0.0f;
    };

    explicit MeterReader(MeterHub&);
    ~MeterReader();
    MeterReader(const MeterReader&) = delete;
    MeterReader& operator=(const MeterReader&) = delete;

    Peak part(int partIndex);
    Peak click();
    Peak cues();
    Peak roleOnBus(int role, int bus);
    Peak bus(int bus);

private:
    friend class MeterHub;
    Peak read(int slot);

    MeterHub& hub_;
    std::vector<float> seen_;   // engine::meter::kSlots × 2 (left, right): the peaks taken since this reader last asked
};

}  // namespace sqlive::app
