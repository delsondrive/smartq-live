#pragma once

#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "engine/Arrangement.h"
#include "engine/CuePlayer.h"
#include "engine/CueSchedule.h"
#include "engine/Meters.h"
#include "engine/Midi.h"
#include "engine/MidiOut.h"
#include "engine/MixSetup.h"
#include "engine/PreparedSong.h"
#include "engine/SongRenderer.h"
#include "engine/SpscQueue.h"
#include "engine/Transition.h"
#include "engine/Types.h"

namespace sqlive::engine {

enum class PlayState : std::uint8_t { Stopped, Playing, Stopping };
enum class FollowKind : std::uint8_t { Stop, Gap, Seamless, Crossfade };
// gapSeconds: the gap's length for Gap, the crossfade's for Crossfade.
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
//
// The cue voice (CuePlayer), out through the mix's cuesRole. While a step plays (not in a lead-in, a count-off, the stop
// fade or the tail after the last step), when song time reaches announceAt of the step's section, the section of
// Arrangement::nextStep() is announced with its sectionCue; nothing while looping or when no step follows. A step entered
// past that moment (startAt) says nothing then, but counts it as passed. Once it has passed, a change of what plays next (a
// choice, a cancel, a loop turned on or off) says the new section's cue at once, or, when that is nothing, silences the
// announcement if it is still speaking; with the song's jump timing NextBar a choice is said at once, before that moment
// too, and one cancelled before the moment is silenced, leaving the moment to say the natural next step. A manual cue is
// said when song time reaches it; a jump skips those it jumps over. A manual cue on the announcement moment wins: the band
// placed it there, so that boundary's announcement is skipped. The schedule is CueSchedule's. A new clip cuts the one
// speaking with a 5 ms fade. The song's last step ending, a stop, and a start all silence the voice with that fade. Cues
// sound at the band's cue level (setCueGain) × the song's cuesAutomation at each sample.
//
// A crossfade follow-on of N seconds (FollowKind::Crossfade): N seconds before the playing song's last step ends, while
// that step plays with nothing chosen or looping and a song is cued, the cue starts from its body start (its count-in
// skipped) and becomes the playing song, just as a seamless follow-on does: Started and its first step reported, its
// starting patches sent, and from then on the click, MIDI, clock and cues are its own (the old song's MIDI ends there, its
// voice is silenced with the 5 ms fade, and its cues are not said). The old song's parts carry on underneath on a second
// renderer, at the levels they had, in a straight line down to silence at its last step's end (no tail after it), then
// Ended for it; the new song's parts fade in over the same samples, while its click is at full level from its first
// sample (the drummer needs the new tempo at once). Less than N seconds left when the moment is worked out: it starts at
// once and lasts what is left. A loop or a choice before the moment puts it off until the last step plays freely
// again; a cue, a follow-on or a loop turned off works it out again; with nothing cued when it comes, the song just ends.
// Worked out from song time, so it doesn't depend on the block size. While a song fades out no other one can: a moment
// that comes meanwhile waits for that fade's end. A stop fades both out over the stop fade and reports Stopped for each;
// a song still fading out when nothing else plays (the new one ended, or is in a gap) falls silent with a stop too.
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
    // Stopped and something cued at the engine's rate: one bar of click (the section's CountOff: the step's first bar, at
    // its tempo and meter, downbeat accented, moved one bar earlier; the parts silent, the click sounding even when the
    // song's is off; the clock devices sent that bar's clock), then that step from its section's start. The MIDI chase for
    // the step's start goes out at the very beginning, and no event written before the step's start is sent (one written
    // there that a device's early offset puts in the count-off is sent at its own time). A step out of range: ignored.
    void startAtStep(int step);
    void stop();
    // For the playing song only (any other serial is ignored), while it plays (not in a gap, the stop fade or its tail).
    // The step to play after the playing one: -1 cancels the choice and any loop; out of range: ignored. Turns looping off.
    void chooseNextStep(std::uint64_t serial, int step);
    // Repeat the playing step at each of its ends, or stop repeating it. Turning it on cancels a chosen step.
    void setLoop(std::uint64_t serial, bool);
    bool setFollowOn(FollowOn);           // false: the command queue was full and nothing was sent
    // Moves on to the cued song now, while a step of the playing song plays (not in its lead-in, a count-off, a gap, the
    // stop fade or its tail) and a song is cued; ignored otherwise, and while an earlier goToNext is still under way or a
    // song still fades out. With a crossfade follow-on (of some length) the crossfade starts at once (as soon as a jump's
    // 240-sample crossfade is over), lasting N seconds, or what is left of a last step; should the cue go, or the follow-on
    // change, before it can begin, it is let go of, and the song plays on as if goToNext had not come. With any other, the playing song
    // ends at the next bar start (or where its step ends, if that comes first), and choices and loops for it are ignored
    // from now on. There, with Seamless (or a crossfade of no length) and a song cued, the next song starts at once while
    // the old one's 50 ms fade plays on the second renderer; otherwise the old song's audio fades out over 50 ms from the
    // bar line, parts only, with no MIDI after it. Either way Ended (not Stopped) is reported for it at the end of that
    // fade, and with Stop or Gap the follow-on then happens as usual (a stop during that fade makes it Stopped, as in any
    // song's tail). A stop before the bar line cancels it. The "hold" and "two taps" safeguards are the control layer's.
    void goToNext();
    void setMix(std::shared_ptr<const MixSetup>);
    void setPartGain(std::uint64_t serial, int part, float linear);
    void setPartMute(std::uint64_t serial, int part, bool mute);
    // A hand on a part's fader: true makes the part ignore its automation (its level is its fader alone) until the next song
    // is set; false lets it follow the automation again. For the playing song only.
    void setPartOverride(std::uint64_t serial, int part, bool overridden);
    void setClickGain(std::uint64_t serial, float linear);
    void setCueGain(std::uint64_t serial, float linear);          // the band's cue level, changed while the song plays
    // Stopped (a song cued or not): says the clip through the cues' outputs at that level, cutting any clip speaking; null
    // silences it. Ignored while a song plays (its stop fade included) or in a gap; a start silences it.
    void previewCue(std::shared_ptr<const PartAudio> clip, float linear);
    // The song's pan of a part (-100 to 100), for the playing song only, gliding.
    void setPartPan(std::uint64_t serial, int part, int pan);
    // The soloed parts of the song with this serial (empty = none; the click and the cue voice are never silenced). For the
    // playing song: at once, the parts gliding. For the cued song: kept with the cue and heard from its first sample when it
    // plays (a start or a follow-on); a new cue, or clearing it, drops it. Any other serial is ignored. By value: no allocation.
    void setSoloSet(std::uint64_t serial, const std::bitset<kMaxParts>&);
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

    // The live meters (Meters.h; EnginePeaks.cpp). Any thread, one taker (app::MeterHub, which hands the peaks on to any
    // number of readers; a second taker would share them with it): a meter's largest left and right magnitude since it was
    // last taken, which then starts again from 0. A slot out of range, or a bus the mix doesn't have, gives {}. Each
    // process raises them at its end, after rendering.
    MeterPeak takePeak(int slot) noexcept;
    // Any thread: someone wants the role meters of this bus (meter::roleOnBus). They are measured from the next block on,
    // until it hasn't been asked for half a second to a second. Allocates nothing, never waits.
    void watchBus(int bus) noexcept;
    // Any thread: the serial of the song the part meters belong to (the playing song as of the last process; 0 = none).
    // When the playing song changes, the engine clears every part meter before it changes this (a release store), so a
    // taker that reads it (acquire) before taking a part meter, and finds a new serial, takes none of the old song's peaks.
    std::uint64_t partMetersSerial() const noexcept;
    int busCount() const noexcept;       // any thread: the mix's buses, as of the last process (at most kMaxBuses)

private:
    struct Command {
        enum class Type : std::uint8_t {
            None, Cue, ClearCue, Start, StartAt, StartAtStep, Stop, SetFollowOn, SetMix, SetPartGain, SetPartMute,
            SetPartOverride, SetClickGain, SetPartPan, SetSoloSet, ResendMidi, ChooseStep, SetLoop, SetCueGain, PreviewCue,
            GoToNext
        };
        Type type = Type::None;
        std::uint64_t serial = 0;             // the song a cue or a level is for
        std::int64_t index = 0;               // a part, a song time, a step, a follow-on kind or a MIDI device
        double value = 0.0;                   // a gain, a mute or a loop (non-zero: on), or a gap in seconds
        std::shared_ptr<const void> object;   // a song, a mix or a cue clip
        std::bitset<kMaxParts> parts{};       // a solo set
    };

    // Objects that could not go onto a full garbage queue wait here, in place, for the next block. A block retires at most
    // one object per command and kRetiresPerRender while rendering, so commands wait while this is nearly full.
    // Rendering retires songs that end, and only the songs in the slots at the block's start can: commands, which alone
    // fill the cue, are taken at the start of a block, and a song enters the playing or the fading slot only from the
    // one before it. So a block's render retires at most kSongSlots songs, even when the fading song's fade ends, the
    // playing song ends and its follow-on ends in the same block. Then it retires the clips the cue voice has finished
    // with, as many as are left of kRetiresPerRender.
    static constexpr int kHeldCapacity = 16;
    static constexpr int kSongSlots = 3;   // playing_, transition_'s fading song (a crossfade's, or goToNext's) and cued_
    static constexpr int kRetiresPerRender = kSongSlots + CuePlayer::kVoices;
    static_assert(kRetiresPerRender >= kSongSlots + CuePlayer::kVoices,
                  "a block's render can retire the playing song, the fading song, the cue and a clip per cue voice");
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
    void renderCueAlone(int numChannels, int frames) noexcept;   // no song set (stopped, a gap): the voice at the preview gain
    // The voice's next frames into cue_buffer_ (null when silent); sounding, if given: how many of the first it sounded in.
    const float* renderCue(std::int64_t frames, std::int64_t* sounding = nullptr) noexcept;
    bool cuesLive() const noexcept;                              // a step plays, so cues are said
    double stopGain(std::int64_t k) const noexcept;   // the stop fade's gain k samples on from where it stands
    double gainAt(std::int64_t k) const noexcept;     // the playing song's: the stop fade's gain times a crossfade's fade-in
    // The song time the playing song's crossfade begins at (Transition::crossfadeAt), when one can: kNever otherwise.
    SamplePos crossfadeDue() const noexcept;
    std::int64_t untilTransition() const noexcept;    // samples to the crossfade's moment or goToNext's end, if ahead
    void beginDueCrossfade(std::int64_t streamTime) noexcept;
    // The playing song moves to the fading slot, fading over `length` samples (with fadeIn, a crossfade), and the cue plays.
    void beginTransition(std::int64_t streamTime, std::int64_t length, bool fadeIn) noexcept;
    void endAtBar(std::int64_t streamTime) noexcept;  // goToNext's end is reached
    void renderFading(int numChannels, std::int64_t frames) noexcept;
    void finishFade(EngineEvent::Type, std::int64_t streamTime) noexcept;   // the fading song is retired, reported so
    void settleFade(std::int64_t streamTime) noexcept;   // finishFade, once the fading song has fallen silent
    SongRenderer& spareRenderer() noexcept;           // the renderer renderer_ is not
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
    // The block's meters onto the published ones, each bus's from out; then the next block's watched buses (EnginePeaks.cpp).
    void publishMeters(float* const* out, int channelCount, int frames) noexcept;
    void aimMeters() noexcept;            // the playing song's renderer meters its parts, the other doesn't

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
    PeakBoard peaks_;                         // every meter's peak since takePeak last took it
    std::atomic<int> bus_count_{0};
    std::atomic<std::uint32_t> watch_asked_{0};   // the buses watchBus asked for since the audio thread last looked
    std::atomic<std::uint64_t> part_meters_serial_{0};   // partMetersSerial

    // Control thread only.
    std::uint64_t next_serial_ = 1;

    // Audio thread only (and prepare, while process is not running).
    std::array<SongRenderer, 2> renderers_;    // the playing song's, and the one a song fading out plays on
    SongRenderer* renderer_ = &renderers_[0];   // the playing song's (and a cue alone's)
    int max_block_ = 0;
    std::vector<float> envelope_;            // max_block_: the fade of one chunk (a stop, a song's tail, a crossfade's
                                             // fade-in, or more than one)
    std::vector<float> click_envelope_;      // max_block_: the click's fade of one chunk while the parts fade in
    std::vector<float*> channels_;           // the output channels from where a chunk's render begins
    SongSlot playing_, cued_;
    std::bitset<kMaxParts> cued_solo_;       // the cued song's solo set (setSoloSet), applied when it starts playing
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
    std::span<const ClickTick> count_off_ticks_;   // its click: the playing song's CountOff for the step
    std::int64_t jump_fade_ = 0;             // 240 samples at 48 kHz: a jump's crossfade, and the tail after a last step
    std::int64_t xfade_left_ = 0;            // samples of a jump's crossfade still to come
    SamplePos tail_time_ = 0;                // during a jump's crossfade: the song time of the audio running on past the line
    bool ending_ = false;                    // the last step is over (or goToNext ended the song): the audio running on
                                             // past it fades out (song_time_)
    std::int64_t tail_left_ = 0;             // samples of that fade still to come
    std::int64_t tail_length_ = 0;           // that fade's length: jump_fade_ after a last step, the stop fade's after goToNext
    std::array<std::shared_ptr<const void>, kHeldCapacity> held_;
    int held_count_ = 0;
    int retired_ = 0;                        // objects retired since this block's commands were taken
    CuePlayer cue_player_;
    std::vector<float> cue_buffer_;          // max_block_: the voice's mono audio for one render
    CueSchedule cue_schedule_;               // what the playing step says, and when
    Transition transition_;                  // a crossfade's moment, goToNext's end, and the song fading out
    std::unique_ptr<BlockMeters> block_meters_ = std::make_unique<BlockMeters>();   // what this block's renders measured
    std::uint32_t watch_kept_ = 0;           // the buses asked for in the last window, watched through this one
    std::int64_t watch_frames_ = 0;          // samples of this window so far (half a second)
    std::uint64_t part_meters_song_ = 0;     // the audio thread's copy of part_meters_serial_
};

} // namespace sqlive::engine
