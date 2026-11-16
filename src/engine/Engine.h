#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/MixSetup.h"
#include "engine/PreparedSong.h"
#include "engine/SongRenderer.h"
#include "engine/SpscQueue.h"
#include "engine/Types.h"

namespace sqlive::engine {

enum class PlayState : std::uint8_t { Stopped, Playing, Stopping };
enum class FollowKind : std::uint8_t { Stop, Gap, Seamless };
struct FollowOn { FollowKind kind = FollowKind::Stop; double gapSeconds = 0.0; };

struct EngineStatus {
    PlayState state = PlayState::Stopped;
    bool inGap = false;
    std::uint64_t playingSerial = 0;   // 0 = none
    std::uint64_t cuedSerial = 0;      // 0 = none
    SamplePos songTime = 0;            // the next sample the playing song will play; 0 when nothing plays
    std::int64_t streamTime = 0;       // samples processed since the engine was made; never goes backwards, even across prepare
};

struct EngineEvent {
    enum class Type : std::uint8_t { Started, Ended, Stopped } type = Type::Started;
    std::uint64_t serial = 0;
    std::int64_t streamTime = 0;       // Started: the song's first sample. Ended, Stopped: the first sample after it.
};

// Plays one song at a time into the device's outputs. The control methods run on the message thread only: each pushes a
// command that process, on the audio thread, takes at the start of its next block. process never allocates, locks or
// frees: what it has finished with goes back through a queue, and collectGarbage frees it on the control thread.
class Engine {
public:
    Engine();

    void prepare(double sampleRate, int maxBlockFrames, int numOutputChannels);   // not while process may run
    void process(float* const* out, int numChannels, int frames) noexcept;        // audio thread: clears out, then renders

    // Control thread (the message thread).
    std::uint64_t cue(std::shared_ptr<const PreparedSong>);   // the song that start() or a follow-on will play; returns its serial
    void clearCue();
    void start();                         // Stopped and something cued: play it from its start
    void startAt(SamplePos songTime);     // the same, from a position clamped to the song
    void stop();
    void setFollowOn(FollowOn);
    void setMix(std::shared_ptr<const MixSetup>);
    void setPartGain(std::uint64_t serial, int part, float linear);
    void setPartMute(std::uint64_t serial, int part, bool mute);
    void setClickGain(std::uint64_t serial, float linear);
    void setSolo(std::uint64_t serial, int part);
    bool popEvent(EngineEvent&);
    void collectGarbage();                // frees what the audio thread has finished with

    EngineStatus status() const noexcept; // any thread; all fields from one publish (a block's end)
    double sampleRate() const noexcept;

private:
    struct Command {
        enum class Type : std::uint8_t {
            None, Cue, ClearCue, Start, StartAt, Stop, SetFollowOn, SetMix, SetPartGain, SetPartMute, SetClickGain, SetSolo
        };
        Type type = Type::None;
        std::uint64_t serial = 0;             // the song a cue or a level is for
        std::int64_t index = 0;               // a part, a song time or a follow-on kind
        double value = 0.0;                   // a gain, a mute (non-zero), or a gap in seconds
        std::shared_ptr<const void> object;   // a song or a mix
    };

    // A song the audio thread holds: the playing one or the cued one. `hold` keeps it alive; `song` is the same object.
    struct SongSlot {
        std::shared_ptr<const void> hold;
        const PreparedSong* song = nullptr;
        std::uint64_t serial = 0;
    };

    // Objects that could not go onto a full garbage queue wait here, in place, for the next block. A block retires at most
    // one object per command and kRetiresPerRender while rendering, so commands wait while this is nearly full.
    // Rendering retires only songs that end, and only the playing song and the cue can: commands, which alone fill the
    // cue, are taken at the start of a block. So a block's render retires at most kSongSlots objects, even when the
    // playing song ends and its follow-on (seamless, or after a gap) ends in the same block.
    static constexpr int kHeldCapacity = 16;
    static constexpr int kRetiresPerRender = 4;
    static constexpr int kSongSlots = 2;   // playing_ and cued_
    static_assert(kRetiresPerRender >= kSongSlots, "a block's render can retire the playing song and the cue");
    static_assert(1 + kRetiresPerRender <= kHeldCapacity, "room for one command and a render");

    void push(Command&&);
    void handle(Command&) noexcept;
    void startPlaying(SamplePos songTime) noexcept;
    void playCued(SamplePos songTime, std::int64_t streamTime, bool snapMix) noexcept;
    void renderChunk(float* const* out, int numChannels, int offset, int frames) noexcept;
    void finishSong(EngineEvent::Type, std::int64_t streamTime) noexcept;
    void endGap(std::int64_t streamTime) noexcept;
    std::int64_t gapFrames() const noexcept;
    void report(EngineEvent::Type, std::uint64_t serial, std::int64_t streamTime) noexcept;
    void retire(std::shared_ptr<const void>&) noexcept;
    void retire(SongSlot&) noexcept;
    void flushHeld() noexcept;
    void publishStatus() noexcept;

    // Shared between the threads.
    SpscQueue<Command> commands_;
    SpscQueue<std::shared_ptr<const void>> garbage_;
    SpscQueue<EngineEvent> events_;
    std::atomic<double> sample_rate_{0.0};
    // The status fields are written together once per process (and by prepare). status_seq_ is odd while they are being
    // written and even once done, so a reader that sees the same even value before and after reading them has one snapshot.
    std::atomic<std::uint64_t> status_seq_{0};
    std::atomic<PlayState> status_state_{PlayState::Stopped};
    std::atomic<bool> status_in_gap_{false};
    std::atomic<std::uint64_t> status_playing_{0};
    std::atomic<std::uint64_t> status_cued_{0};
    std::atomic<SamplePos> status_song_time_{0};
    std::atomic<std::int64_t> status_stream_time_{0};

    // Control thread only.
    std::uint64_t next_serial_ = 1;

    // Audio thread only (and prepare, while process is not running).
    SongRenderer renderer_;
    int max_block_ = 0;
    std::vector<float> envelope_;            // max_block_: the stop fade of one chunk
    std::vector<float*> channels_;           // the output channels from where a chunk's render begins
    SongSlot playing_, cued_;
    std::shared_ptr<const void> mix_hold_;
    const MixSetup* mix_ = nullptr;
    FollowOn follow_;                        // what follows when the playing song ends, read at that moment
    PlayState state_ = PlayState::Stopped;   // Playing during a gap too
    bool in_gap_ = false;                    // silence between a song that ended and its follow-on; playing_ is empty
    std::int64_t gap_left_ = 0;              // samples of the gap still to come
    std::uint64_t gap_after_ = 0;            // the serial of the song that ended before the gap, for a stop inside it
    SamplePos song_time_ = 0;
    std::int64_t stream_time_ = 0;
    std::int64_t fade_length_ = 0;
    std::int64_t fade_pos_ = 0;              // samples of the stop fade already played
    std::array<std::shared_ptr<const void>, kHeldCapacity> held_;
    int held_count_ = 0;
};

} // namespace sqlive::engine
