#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/Arrangement.h"
#include "engine/Midi.h"
#include "engine/MidiOut.h"
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
    int step = -1;                     // the arrangement step playing; -1 when none (lead-in, after the last step, stopped)
    int pendingStep = -1;              // the step chosen to come next; -1 = the one that follows
    bool looping = false;              // the playing step repeats at its end
    bool countOff = false;             // startAtStep's bar of click is playing (step is -1 meanwhile)
};

struct EngineEvent {
    enum class Type : std::uint8_t { Started, Ended, Stopped, StepChanged } type = Type::Started;
    std::uint64_t serial = 0;
    std::int64_t streamTime = 0;       // Started: the song's first sample. Ended, Stopped: the first sample after it.
                                       // StepChanged: the step's first sample
    bool inGap = false;                // Stopped during a gap: `serial` is the song that had already ended before it
    int step = -1;                     // StepChanged: the step now playing (never -1)
};

// The most device channels process renders into; any beyond these are cleared and left silent.
inline constexpr int kMaxDeviceChannels = 256;

// Plays one song at a time into the device's outputs. The control methods run on the message thread only: each pushes a
// command that process, on the audio thread, takes at the start of its next block. process never allocates, locks or
// frees: what it has finished with goes back through a queue, and collectGarbage frees it on the control thread.
//
// A song plays as its arrangement: the playhead is a step and a song time. Playback starts in the lead-in (count-in or
// pre-roll, song time below 0, no step) when the song has one, and at song time 0 enters step 0. Where a step's section
// ends, the next step plays (Arrangement::nextStep: the playing step again while looping, else the step chosen, else the
// one after): straight on when its section starts there, otherwise as a jump, which lands on the section's start with a
// crossfade of 240 samples (scaled by sampleRate / 48000) from the audio running on past the line (parts only) to the
// incoming audio. With the song's jump timing NextBar, a step chosen or a loop turned on during a step takes effect at
// the next bar start instead, if that comes first. A choice or a loop made in the lead-in applies after the first step.
// A start with nothing yet heard enters its first step cleanly, without a crossfade: from song time 0 on, after a count-in
// with no pre-roll audio, or after a count-off; only pre-roll audio that is playing crossfades into a first step that
// isn't at bar 1. On every jump the MIDI is chased (MidiOut::jump). The song ends when its last step does; when that is
// not where its audio ends, the audio running on fades out over the same 240 samples before Ended. Each step entered is
// reported (StepChanged), the first included, a song without sections too (its one step, 0, is the whole song). stop and
// a song's end clear the choice and the loop.
class Engine {
public:
    Engine();

    // Must only be called while process cannot run: for a device, from audioDeviceAboutToStart, before its first callback.
    // It stops whatever was playing, at once and without an event (a note-off goes out for each MIDI note it left
    // sounding, and 0xFC to each device it started), keeps the mix, keeps the cue unless it was made at another sample
    // rate (that one is dropped, through the garbage queue), and keeps stream time running. Every command not yet taken
    // is dropped, a start included, and what it carries goes through the garbage queue: the control thread sends again
    // what it still wants (the mix, the follow-on, the cue) once the device has started.
    void prepare(double sampleRate, int maxBlockFrames);
    // Must only be called while process cannot run, as prepare: when the device stopped by itself (lost, or failed), after a
    // deliberate close or a failed open, and from the host's destructor, before any device starts again. The MIDI can't wait for
    // that: a note-off goes out now for each note left sounding, and 0xFC
    // to each device the song started, at the stream time the last block ended. The song itself stops at the next
    // prepare, whose own note-offs and stops then find nothing left to send.
    void deviceStopped();
    // Audio thread: clears out, then renders into the first min(numChannels, kMaxDeviceChannels) of it.
    void process(float* const* out, int numChannels, int frames) noexcept;

    // Control thread (the message thread).
    // The song that start() or a follow-on will play; returns its serial. When the command queue is full the cue is not
    // queued: it returns 0, and the serial it would have had is used up all the same, so serials only ever go up.
    std::uint64_t cue(std::shared_ptr<const PreparedSong>);
    bool clearCue();                      // false: the command queue was full and nothing was sent
    void start();                         // Stopped and something cued at the engine's rate: play it from its start
    // The same, from a position clamped to the song (at its start: as start()). Before 0: the lead-in from there, then
    // step 0. At 0: step 0, from its section's start, count-in or not. Otherwise the first step whose section holds it,
    // from there; else the first step whose section starts after it, from that start; else the song ends at once.
    void startAt(SamplePos songTime);
    // Stopped and something cued at the engine's rate: one bar of click (a count-off at the tempo of the step's first bar,
    // the parts silent, the click sounding even when the song's is off), then that step from its section's start. The MIDI
    // chase for the step's start goes out at the very beginning, and no event before the step's start is sent. A step out
    // of range: ignored.
    void startAtStep(int step);
    void stop();
    // For the playing song only (any other serial is ignored), while it plays (not in a gap, the stop fade or its tail).
    // The step to play after the playing one: -1 cancels the choice and any loop; out of range: ignored. Turns looping off.
    void chooseNextStep(std::uint64_t serial, int step);
    // Repeat the playing step at each of its ends, or stop repeating it. Turning it on cancels a chosen step.
    void setLoop(std::uint64_t serial, bool);
    bool setFollowOn(FollowOn);           // false: the command queue was full and nothing was sent
    void setMix(std::shared_ptr<const MixSetup>);
    void setPartGain(std::uint64_t serial, int part, float linear);
    void setPartMute(std::uint64_t serial, int part, bool mute);
    void setClickGain(std::uint64_t serial, float linear);
    void setSolo(std::uint64_t serial, int part);
    bool popEvent(EngineEvent&);
    // One consumer thread (the MIDI dispatcher): the MIDI the playing song sends, oldest first, each with the stream time
    // it is due at. Notes still sounding when a song stops (at the end of its fade), ends, or prepare stops it are ended
    // then. When the queue is full, what doesn't fit is dropped. A song that starts first sends each device the state it
    // should be in (program with its bank, controllers), as of its first step's start when it starts from its start
    // (start(), a startAt at or before the song's start) or follows on, as of the step's start for startAtStep, as of
    // where it lands for startAt otherwise, at the stream time of its first sample; the changes that covered are not sent
    // again. A jump ends the notes sounding and sends only the state at the landing that differs from what each device
    // was last sent, a change exactly on the landing included. Then each of its start-stop devices is sent 0xFA, and
    // each clock device 0xF8 at every one of its clockTicks it plays. When it stops or ends, its start-stop devices are
    // sent 0xFC, unless another song follows straight on (seamlessly, or after a gap of no length): the transport runs
    // on, and the clock with it. A gap sends no clock.
    bool popMidi(OutMidi&);
    // Any thread: how many MIDI messages have been queued for popMidi since the engine was made (so a reader can tell when
    // it has taken them all).
    std::uint64_t queuedMidiCount() const noexcept;
    // Any thread: how many MIDI messages a full queue has dropped since the engine was made. For diagnostics only.
    std::uint64_t droppedMidiCount() const noexcept;
    // Control thread: sends one device (an engine device number of the song with this serial) the state it should be in
    // now, at the start of the next block. Nothing unless that song is the one playing (so nothing in a gap or stopped).
    void resendMidiState(std::uint64_t serial, int device);
    // Frees what the audio thread has finished with. Call it regularly (the app's timer does): when the garbage queue is
    // full, the audio thread keeps what it lets go of in a small store of its own, and once that is nearly full it holds
    // back every command, cues, starts and stops included, until garbage is collected.
    void collectGarbage();

    EngineStatus status() const noexcept; // any thread; all fields from one publish (a block's end)
    double sampleRate() const noexcept;

private:
    struct Command {
        enum class Type : std::uint8_t {
            None, Cue, ClearCue, Start, StartAt, StartAtStep, Stop, SetFollowOn, SetMix, SetPartGain, SetPartMute,
            SetClickGain, SetSolo, ResendMidi, ChooseStep, SetLoop
        };
        Type type = Type::None;
        std::uint64_t serial = 0;             // the song a cue or a level is for
        std::int64_t index = 0;               // a part, a song time, a step, a follow-on kind or a MIDI device
        double value = 0.0;                   // a gain, a mute or a loop (non-zero: on), or a gap in seconds
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

    bool push(Command&&);                 // false: the queue was full, and the command was dropped
    void handle(Command&) noexcept;
    void startPlaying(SamplePos songTime, bool fromStart) noexcept;
    bool fitsRate(const PreparedSong&) const noexcept;
    bool cueFits() const noexcept;        // something is cued, made at the engine's rate; a cue that doesn't never plays
    void playCued(SamplePos songTime, std::int64_t streamTime, bool snapMix, bool fromStart) noexcept;
    void playCountOff(int step) noexcept;
    void takeCue() noexcept;
    void beginPlaying(SamplePos songTime, std::int64_t streamTime, bool snapMix) noexcept;
    void renderChunk(float* const* out, int numChannels, int offset, int frames) noexcept;
    std::int64_t renderStraight(int numChannels, std::int64_t frames, std::int64_t streamTime) noexcept;
    std::int64_t renderCrossfade(int numChannels, std::int64_t frames, std::int64_t streamTime) noexcept;
    std::int64_t renderTail(int numChannels, std::int64_t frames) noexcept;
    double stopGain(std::int64_t k) const noexcept;   // the stop fade's gain k samples on from where it stands
    void settle(std::int64_t streamTime) noexcept;
    bool handOver(std::int64_t streamTime) noexcept;
    void enterStep(int step, std::int64_t streamTime) noexcept;
    void finishSong(EngineEvent::Type, std::int64_t streamTime) noexcept;
    void endGap(std::int64_t streamTime) noexcept;
    std::int64_t gapFrames() const noexcept;
    void report(EngineEvent::Type, std::uint64_t serial, std::int64_t streamTime, bool inGap = false,
                int step = -1) noexcept;
    void retire(std::shared_ptr<const void>&) noexcept;
    void retire(SongSlot&) noexcept;
    void flushHeld() noexcept;
    void publishStatus() noexcept;

    // Shared between the threads.
    SpscQueue<Command> commands_;
    SpscQueue<std::shared_ptr<const void>> garbage_;
    SpscQueue<EngineEvent> events_;
    MidiOut midi_;                           // its queue too; its producer is the audio thread (and prepare)
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
    std::atomic<int> status_step_{-1};
    std::atomic<int> status_pending_step_{-1};
    std::atomic<bool> status_looping_{false};
    std::atomic<bool> status_count_off_{false};

    // Control thread only.
    std::uint64_t next_serial_ = 1;

    // Audio thread only (and prepare, while process is not running).
    SongRenderer renderer_;
    int max_block_ = 0;
    std::vector<float> envelope_;            // max_block_: the fade of one chunk (a stop, a song's tail, or both)
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
    Arrangement arrangement_;                // the playing song's steps, which plays, and what was chosen to come next
    SamplePos boundary_ = 0;                 // arrangement_.boundary as worked out when the step (or the lead-in) began,
                                             // or a choice or a loop arrived: where the playing step hands over
    bool count_off_ = false;                 // startAtStep's bar of click, [count_off_from_, count_off_to_), is playing
    SamplePos count_off_from_ = 0, count_off_to_ = 0;
    std::int64_t jump_fade_ = 0;             // 240 samples at 48 kHz: a jump's crossfade, and the tail after a last step
    std::int64_t xfade_left_ = 0;            // samples of a jump's crossfade still to come
    SamplePos tail_time_ = 0;                // during a crossfade: the song time of the audio running on past the line
    bool ending_ = false;                    // the last step is over: the audio running on past it fades out (song_time_)
    std::int64_t tail_left_ = 0;             // samples of that fade still to come
    std::array<std::shared_ptr<const void>, kHeldCapacity> held_;
    int held_count_ = 0;
};

} // namespace sqlive::engine
