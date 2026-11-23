#include "ui/Screens.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <utility>

#include <juce_audio_formats/juce_audio_formats.h>

#include "app/AudioHost.h"
#include "app/ControlInput.h"
#include "app/LevelChecker.h"
#include "app/MidiPorts.h"
#include "app/RemoteAccess.h"
#include "app/RemoteServer.h"
#include "app/Session.h"
#include "app/Settings.h"
#include "app/ShowController.h"
#include "model/ActionNames.h"
#include "model/DemoBand.h"
#include "model/DocumentsJson.h"
#include "model/JsonObject.h"
#include "model/Snapshots.h"
#include "ui/AppShell.h"
#include "ui/EffectsWindow.h"
#include "ui/MainView.h"
#include "ui/MixerPage.h"
#include "ui/NewComputerPage.h"
#include "ui/Pages.h"
#include "ui/RemoteSetupPage.h"
#include "ui/SetlistsPage.h"
#include "ui/SetupPage.h"
#include "ui/SongEditorPage.h"
#include "ui/WelcomePage.h"

namespace sqlive::ui {

namespace {

using app::Action;
using app::Mode;

constexpr int kRate = 48000;

// Runs the message thread's pending work: change messages, async updates and timers the views wait on.
void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(30); }

// A short tone as a 16-bit mono WAV: what the pictures' voice and the band's own recording say.
bool writeTone(const juce::File& file, double seconds, double hz) {
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr) return false;
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                  .withSampleRate(kRate)
                                                  .withNumChannels(1)
                                                  .withBitsPerSample(16));
    if (writer == nullptr) return false;
    juce::AudioBuffer<float> tone(1, static_cast<int>(seconds * kRate));
    for (int i = 0; i < tone.getNumSamples(); ++i)
        tone.setSample(0, i, 0.3f * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * hz * i / kRate)));
    const bool written = writer->writeFromAudioSampleBuffer(tone, 0, tone.getNumSamples());
    writer.reset();                                               // writes the header and closes the file
    return written;
}

// The pedalboards' ports in the pictures: two outputs that are always there and send nowhere.
class SilentOutput : public app::MidiPorts::Output {
public:
    void send(const std::uint8_t*, int) override {}
};
const char* const kPorts[] = {"USB MIDI Port A", "USB MIDI Port B"};

// A demo band in a temp folder of its own, with its own settings file there, a session on it with no real device, and the
// shell wired as MainWindow wires it. The band has its MIDI as the app makes it, each device bound on this computer to a
// port of its own that MidiPorts finds but that sends nowhere, and the pedals are learned through a ControlInput that
// opens no input. An AudioHost only when asked for, and then driven only through its test hooks. Everything goes, the
// temp folder last, when this does.
class Scene {
public:
    explicit Scene(bool withHost)
        : folder_(juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("sqlive-screens-" + juce::Uuid().toDashedString())) {
        if (!folder_.getChildFile("Documents").createDirectory()) return;
        const auto root = folder_.getChildFile("Documents").getChildFile("Demo band");   // "Documents › Demo band" on Welcome
        if (model::createDemoBand(root).failed()) return;
        settings_ = std::make_unique<app::Settings>(folder_.getChildFile("settings.json"));
        settings_->noteOpened(root);
        session_ = std::make_unique<app::Session>(*settings_, root);
        session_->setDevice(kRate, 2);
        // As the app makes the demo band: with its pedalboards and their changes, then its sections, which Undo can't take
        // away.
        if (model::addDemoMidi(session_->commands()).failed() || model::addDemoSections(session_->commands()).failed()
            || !bindPorts() || !tuneDevices())
            return;
        session_->commands().clearHistory();
        session_->flushNow();                                     // saved, as a band opened a moment ago is
        midi_ = std::make_unique<app::MidiPorts>(
            *session_, [] { return juce::StringArray(kPorts, (int) std::size(kPorts)); },
            [](const juce::String&) -> std::unique_ptr<app::MidiPorts::Output> { return std::make_unique<SilentOutput>(); });
        session_->attachMidi(midi_.get());
        midi_->refresh();
        if (withHost) host_ = std::make_unique<app::AudioHost>(*session_);
        controller_ = std::make_unique<app::ShowController>(*session_);
        input_ = std::make_unique<app::ControlInput>(
            *session_, *controller_, [] { return juce::StringArray(); },
            [](const juce::String&, juce::MidiInputCallback*) -> std::unique_ptr<juce::MidiInput> { return nullptr; });
        access_ = std::make_unique<app::RemoteAccess>(*settings_, root);
        server_ = std::make_unique<app::RemoteServer>(*session_, *access_, [] { return juce::String("{}"); });   // never started
        shell_ = std::make_unique<AppShell>(*session_, host_.get());
        shell_->setShowController(controller_.get());
        plugInPages(*shell_, *session_, host_.get(), controller_.get(), input_.get(), access_.get(), server_.get());
        settle();
    }

    ~Scene() {
        shell_.reset();
        server_.reset();
        access_.reset();
        input_.reset();
        controller_.reset();
        host_.reset();
        if (session_ != nullptr) session_->attachMidi(nullptr);
        midi_.reset();
        session_.reset();                                         // flushes into the temp folder
        settings_.reset();
        pump();
        folder_.deleteRecursively();
    }

    bool ok() const { return shell_ != nullptr; }
    app::Session& session() { return *session_; }
    app::Settings& settings() { return *settings_; }
    app::RemoteAccess& remoteAccess() { return *access_; }
    app::RemoteServer& remoteServer() { return *server_; }
    app::AudioHost& host() { return *host_; }
    AppShell& shell() { return *shell_; }
    const juce::File& folder() const { return folder_; }

    // Waits for the songs to load, then runs what the message thread has pending.
    void settle() {
        session_->cache().waitUntilIdle(20000);
        pump();
    }

    juce::String songIdAt(int index) const {
        const auto* setlist = session_->model().setlist(session_->setlistId());
        return setlist != nullptr && index < (int) setlist->entries.size() ? setlist->entries[(size_t) index].songId : juce::String();
    }

    void wantStageView(bool stage) {
        if (session_->stageView() != stage) session_->perform(Action::ToggleView);
    }

    // Plays the setlist's next song until its clock reads `seconds`.
    void playUntil(double seconds) {
        session_->perform(Action::Start);
        for (int i = 0; i < 6000 && session_->now().seconds < seconds; ++i) session_->advanceWithoutDevice(kRate / 100);
    }

    // Plays the next song to its end (the demo set stops after each song).
    void playThrough() {
        session_->perform(Action::Start);
        session_->advanceWithoutDevice(kRate / 10);
        for (int i = 0; i < 20000 && session_->now().playing; ++i) session_->advanceWithoutDevice(kRate / 10);
    }

    template <typename PageType>
    PageType* page(Page which) {
        shell_->showPage(which);
        return dynamic_cast<PageType*>(shell_->currentContent());
    }

    // The level check run to its end on the setlist as it is mixed now.
    void measureLevels() {
        session_->levelChecker().checkSetlist();
        session_->levelChecker().waitUntilIdle(30000);
        pump();
    }

private:
    // Each of the band's devices on a port of its own on this computer, in the current rig.
    bool bindPorts() {
        const auto& devices = session_->model().band().midiDevices;
        for (size_t i = 0; i < devices.size() && i < std::size(kPorts); ++i) {
            auto* params = new juce::DynamicObject();
            params->setProperty("rigId", session_->rigId());
            params->setProperty("deviceId", devices[i].id);
            params->setProperty("computerId", settings_->computerName());
            params->setProperty("port", kPorts[i]);
            if (!session_->commands().run("rig.setMidi", juce::var(params)).ok) return false;
        }
        return true;
    }

    // As the mockup's screens 9 and 12 have them: Guitar 1 nudged 20 ms early, Guitar 2 sent the clock.
    bool tuneDevices() {
        const auto& devices = session_->model().band().midiDevices;
        if (devices.size() < 2) return false;
        const auto first = devices[0].id, second = devices[1].id;
        return session_->commands().run("device.set", model::jsonObject({{"deviceId", first}, {"offsetMs", -20}})).ok
            && session_->commands().run("device.set", model::jsonObject({{"deviceId", second}, {"clock", true}})).ok;
    }

    juce::File folder_;
    std::unique_ptr<app::Settings> settings_;
    std::unique_ptr<app::Session> session_;
    std::unique_ptr<app::MidiPorts> midi_;
    std::unique_ptr<app::AudioHost> host_;
    std::unique_ptr<app::ShowController> controller_;
    std::unique_ptr<app::ControlInput> input_;
    std::unique_ptr<app::RemoteAccess> access_;                  // for Setup's Remote page; its server is never started
    std::unique_ptr<app::RemoteServer> server_;
    std::unique_ptr<AppShell> shell_;
};

juce::Image snapshot(juce::Component& component, int width, int height) {
    component.setBounds(0, 0, width, height);
    pump();                                                       // what a resize posted
    return component.createComponentSnapshot(component.getLocalBounds());
}

// A MIDI file exported from the DAW with the stems: a tempo change, markers, and a track of program changes per pedalboard,
// named so the app suggests the demo band's devices for them.
juce::File midiFileFor(const juce::File& folder) {
    constexpr int ppq = 480, bar = ppq * 4;
    const auto at = [](juce::MidiMessage m, int ticks) { m.setTimeStamp(ticks); return m; };
    const auto marker = [&](const char* text, int barNumber) {
        return at(juce::MidiMessage::textMetaEvent(6, text), (barNumber - 1) * bar);
    };
    const auto trackName = [&](const char* text) { return at(juce::MidiMessage::textMetaEvent(3, text), 0); };
    const std::vector<std::vector<juce::MidiMessage>> tracks = {
        {at(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0), at(juce::MidiMessage::tempoMetaEvent(60000000 / 112), 0),
         at(juce::MidiMessage::tempoMetaEvent(60000000 / 120), 16 * bar), marker("Intro", 1), marker("Verse 1", 5),
         marker("Chorus", 13), marker("Outro", 21), at(juce::MidiMessage::endOfTrack(), 24 * bar)},
        {trackName("Gtr 1 FX"), at(juce::MidiMessage::programChange(1, 4), 0),
         at(juce::MidiMessage::programChange(1, 12), 12 * bar), at(juce::MidiMessage::programChange(1, 20), 20 * bar)},
        {trackName("Gtr 2 FX"), at(juce::MidiMessage::programChange(2, 2), 0), at(juce::MidiMessage::programChange(2, 9), 20 * bar)},
    };
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(ppq);
    for (const auto& track : tracks) {
        juce::MidiMessageSequence sequence;
        for (const auto& message : track) sequence.addEvent(message);
        midi.addTrack(sequence);
    }
    const auto file = folder.getChildFile("Night Drive.mid");
    file.deleteFile();
    if (auto out = file.createOutputStream(); out == nullptr || !midi.writeTo(*out, 1)) return {};
    return file;
}

// The stems a band would drop for a new song: three the app recognises, one it can't place, and a click it leaves out;
// and the song's MIDI file.
juce::Array<juce::File> stemsFor(Scene& scene) {
    const auto stems = scene.folder().getChildFile("Night Drive");
    stems.createDirectory();
    juce::Array<juce::File> files;
    const auto* song = scene.session().model().song(scene.songIdAt(0));
    if (song == nullptr) return files;
    const std::pair<const char*, int> made[] = {{"ND_Keys.wav", 0}, {"ND_Bass.wav", 1}, {"ND_Perc.wav", 2},
                                                {"ND_Audio 04.wav", 0}, {"ND_Click.wav", 2}};
    for (const auto& [name, part] : made) {
        if (part >= (int) song->parts.size()) continue;
        const auto target = stems.getChildFile(name);
        if (scene.session().folder().audioFile(*song, song->parts[(size_t) part]).copyFileTo(target)) files.add(target);
    }
    if (const auto midi = midiFileFor(stems); midi.existsAsFile()) files.add(midi);
    return files;
}

// The id of the song's change on that device's lane at that bar; "" when there is none.
juce::String changeAt(const app::Session& session, const juce::String& songId, const juce::String& deviceName, int bar,
                      juce::String& deviceId) {
    const auto* song = session.model().song(songId);
    if (song == nullptr) return {};
    for (const auto& device : session.model().band().midiDevices) {
        if (device.name != deviceName) continue;
        deviceId = device.id;
        if (const auto lane = song->midiLanes.find(device.id); lane != song->midiLanes.end())
            for (const auto& marker : lane->second)
                if (marker.bar == bar) return marker.id;
    }
    return {};
}

// The demo song's "Long ending" (model::addDemoSections) and its Chorus; "" when it has none.
std::pair<juce::String, juce::String> longEndingOf(const app::Session& session, const juce::String& songId) {
    const auto* song = session.model().song(songId);
    if (song == nullptr) return {};
    juce::String arrangementId, chorusId;
    for (const auto& arrangement : song->arrangements)
        if (arrangement.name == "Long ending") arrangementId = arrangement.id;
    for (const auto& section : song->sections)
        if (section.name == "Chorus") chorusId = section.id;
    return {arrangementId, chorusId};
}

// As the mockup's screens 1, 2 and 7 have the set: carrying on, the first song on its "Long ending" and then a 4 s
// crossfade into the second, which is followed by a 4 s gap.
void longEndingThenCrossfade(app::Session& session, const juce::String& songId) {
    const auto arrangementId = longEndingOf(session, songId).first;
    const auto* list = session.model().setlist(session.setlistId());
    if (arrangementId.isEmpty() || list == nullptr || list->entries.size() < 2) return;
    const auto first = list->entries[0].id, second = list->entries[1].id, setlistId = session.setlistId();
    session.commands().run("setlist.set", model::jsonObject({{"setlistId", setlistId}, {"playMode", "continuous"}}));
    session.commands().run("setlist.setArrangement",
                           model::jsonObject({{"setlistId", setlistId}, {"entryId", first}, {"arrangementId", arrangementId}}));
    session.commands().run("setlist.setTransition", model::jsonObject({{"setlistId", setlistId}, {"entryId", first},
                                                                       {"kind", "crossfade"}, {"seconds", 4}}));
    session.commands().run("setlist.setTransition", model::jsonObject({{"setlistId", setlistId}, {"entryId", second},
                                                                       {"kind", "gap"}, {"seconds", 4}}));
}

// As the mockup's screens 1 and 9 have it: "Watch me" said in the Chorus, between the automatic cues.
void addWatchMe(app::Session& session, const juce::String& songId) {
    session.commands().run("cue.add", model::jsonObject({{"songId", songId}, {"bar", 10}, {"beat", 1.0},
                                                         {"clip", "cues/built-in/Watch me.wav"}, {"text", "Watch me"}}));
}

// The meters a moment into the music, as a view sees them: another half second played in frames of a thirtieth of a
// second, each followed by a frame of the view's meters, so the bars, their hold lines and the peak boxes show what the
// music does there.
void playMeterFrames(app::Session& session, const std::function<void(double)>& frame) {
    for (int i = 0; i < 15; ++i) {
        session.advanceWithoutDevice(kRate / 30);
        frame(juce::Time::getMillisecondCounterHiRes());
    }
}

juce::String roleNamed(const app::Session& session, const juce::String& name) {
    for (const auto& role : session.model().band().roles)
        if (role.name == name) return role.id;
    return {};
}

// As the mockup's #song has the parts: Keys a little down and to the left, Bass centred-right, Perc up and to the right.
void dressSongMix(app::Session& session, const juce::String& songId) {
    const auto* song = session.model().song(songId);
    if (song == nullptr) return;
    const std::map<juce::String, std::pair<float, int>> look = {{"Keys", {-3.0f, -40}}, {"Bass", {0.0f, 15}}, {"Perc", {2.0f, 55}}};
    for (const auto& part : song->parts)
        if (const auto it = look.find(part.name); it != look.end()) {
            const app::LevelTarget target{app::LevelTarget::Kind::Part, songId, part.id, {}, {}};
            session.setLevel(target, it->second.first, "screens");
            session.setPan(target, it->second.second, "screens");
        }
}

// As the mockup's #output has the first output's mix: each role at its own level, Keys and Guitars placed by the output
// (unlinked), BVs not sent.
void dressOutputMix(app::Session& session, const juce::String& outputId) {
    const std::map<juce::String, std::pair<std::optional<float>, std::optional<int>>> look = {
        {"Cues", {-3.0f, {}}},  {"Keys", {-6.0f, -60}}, {"Synth", {-12.0f, {}}}, {"Guitars", {-4.0f, 40}},
        {"BVs", {{}, {}}},      {"Perc", {-6.0f, {}}},  {"Strings", {-12.0f, {}}}};
    for (const auto& [roleName, levelAndPan] : look) {
        const auto roleId = roleNamed(session, roleName);
        if (roleId.isEmpty()) continue;
        const app::LevelTarget target{app::LevelTarget::Kind::Mix, {}, {}, outputId, roleId};
        session.setLevel(target, levelAndPan.first, "screens " + roleName);
        if (levelAndPan.second) session.setPan(target, levelAndPan.second, "screens pan " + roleName);
    }
}

juce::String partNamed(const app::Session& session, const juce::String& songId, const juce::String& name) {
    if (const auto* song = session.model().song(songId))
        for (const auto& part : song->parts)
            if (part.name == name) return part.id;
    return {};
}

// The mockup's EQ on Keys: a 12 dB per octave cut at 60 Hz, a dip of 3.5 dB at 310 Hz (Q 1.2), a shelf of +2.5 dB at 6 kHz.
model::EffectDoc withKeysEq(model::EffectDoc eq) {
    const auto band = [](int shape, double hz, double gainDb, double q) {
        auto* b = new juce::DynamicObject();
        b->setProperty("shape", shape);  b->setProperty("freq", hz);  b->setProperty("gain", gainDb);  b->setProperty("q", q);
        b->setProperty("slope", 1);
        return juce::var(b);
    };
    juce::Array<juce::var> bands;
    bands.add(band(3, 60.0, 0.0, 0.71));                          // smartq::dsp::Shape: Bell 0, LowShelf 1, HighShelf 2, LowCut 3
    bands.add(band(0, 310.0, -3.5, 1.2));
    bands.add(band(2, 6000.0, 2.5, 0.71));
    eq.values.set("bands", bands);
    return eq;
}

juce::Image render(const juce::String& name, int width, int height) {
    Scene scene(name == "interrupted" || name == "new-computer" || name == "routing");
    if (!scene.ok()) return {};
    auto& session = scene.session();
    auto& shell = scene.shell();
    // The shell as the window shows it: in Edit mode with its window buttons (not maximised); in Show mode with none, since
    // on stage Show mode is full screen (kiosk).
    const auto shot = [&] {
        shell.setWindowControls(session.mode() == Mode::Edit, false);
        return snapshot(shell, width, height);
    };

    if (name == "main-view" || name == "stage-playing" || name == "stage-looping") {
        // The first song on its "Long ending", then a crossfade, with "Watch me" in its Chorus; played into that Chorus,
        // and the Outro chosen to come next with ↓, so the Chorus isn't played again (or, looping, the Chorus looped with L).
        longEndingThenCrossfade(session, scene.songIdAt(0));
        addWatchMe(session, scene.songIdAt(0));
        scene.settle();
        session.enterShow();
        scene.wantStageView(name != "main-view");
        const auto* band = &session.model().band();
        session.setLevel({app::LevelTarget::Kind::BandClick, {}, {}, {}, {}}, band->clickDb + 2.0f, "screens");
        scene.playUntil(22.0);                                    // bar 10
        session.perform(name == "stage-looping" ? Action::LoopSection : Action::NextSectionLater);
        session.advanceWithoutDevice(kRate / 100);
        pump();
        if (auto* view = dynamic_cast<MainView*>(shell.currentContent()))
            playMeterFrames(session, [view](double now) { view->mixer().tickMeters(now); });
    } else if (name == "stage-between" || name == "stage-loading") {
        // The second song next, in its eight sections.
        session.enterShow();
        scene.wantStageView(true);
        scene.playThrough();
        session.chooseNext(1);
        scene.settle();
        if (name == "stage-loading") {
            session.cache().setPaused(true);
            session.cache().forget(session.now().nextSongId);
            pump();
            auto image = shot();
            session.cache().setPaused(false);
            scene.settle();
            return image;
        }
    } else if (name == "preshow-check") {
        scene.measureLevels();                                    // the Levels line: "no output clips in any of the 3 songs"
        shell.requestMode(Mode::Show);
    } else if (name == "interrupted") {
        session.enterShow();
        scene.wantStageView(true);
        scene.playUntil(20.0);
        scene.host().deviceLost();
    } else if (name == "welcome") {
        WelcomePage welcome(scene.settings());
        return snapshot(welcome, width, height);
    } else if (name == "new-computer") {
        NewComputerPage page(session, scene.host());
        return snapshot(page, width, height);
    } else if (name == "setlists") {
        // The mockup's row 3: the first song on its "Long ending" arrangement, then a 4 s crossfade; the second, a gap.
        longEndingThenCrossfade(session, scene.songIdAt(0));
        scene.page<SetlistsPage>(Page::Setlists);
    } else if (name == "new-song") {
        if (auto* setlists = scene.page<SetlistsPage>(Page::Setlists); setlists != nullptr && setlists->onNewSong)
            setlists->onNewSong(stemsFor(scene));
    } else if (name == "song-editor") {
        const auto [arrangementId, chorusId] = longEndingOf(session, scene.songIdAt(0));
        addWatchMe(session, scene.songIdAt(0));
        if (auto* setlists = scene.page<SetlistsPage>(Page::Setlists); setlists != nullptr && setlists->onOpenSong)
            setlists->onOpenSong(scene.songIdAt(0));              // as a double-click does: the editor, and the Mixer's song
        if (auto* editor = scene.page<SongEditorPage>(Page::SongEditor)) {
            editor->arrangementRow().choose(arrangementId);       // "Long ending", with its Chorus just put in once more
            editor->arrangementRow().insertSection(chorusId, 4);
            juce::String deviceId;                                // a change on Guitar 1 at bar 9
            changeAt(session, scene.songIdAt(0), "Guitar 1", 9, deviceId);
            // The mixer under the tracks as the mockup's #editor has it: the song mixed, one part soloed, Bass's track and
            // strip selected, and the song played to bar 10 with the meters moving.
            const auto songId = scene.songIdAt(0);
            dressSongMix(session, songId);
            if (const auto* song = session.model().song(songId))
                for (const auto& part : song->parts) {
                    if (part.name == "Perc") session.setSolo(songId, part.id, true);
                    if (part.name == "Bass") editor->timeline().select({EditorSelection::Kind::Part, {}, part.id});
                }
            session.playSong(songId, 0.0);
            for (int i = 0; i < 6000 && session.now().seconds < 21.6; ++i) session.advanceWithoutDevice(kRate / 100);
            pump();
            auto& mixer = editor->mixerDock().mixer();
            mixer.tickMeters(juce::Time::getMillisecondCounterHiRes());
            playMeterFrames(session, [&mixer](double now) { mixer.tickMeters(now); });
        }
        session.flushNow();                                       // "Saved ✓", as the mockup has it
    } else if (name == "midi-setup") {
        // A footswitch learned for Start and one for Stop, as the mockup has them: CC 64 and CC 65 on channel 10.
        for (const auto& [action, controller] : {std::pair{"start", 64}, std::pair{"stop", 65}}) {
            const auto* info = model::actionInfo(action);
            if (info == nullptr) continue;
            auto binding = info->defaultBinding;
            binding.midiKind = model::MidiKind::Control;  binding.midiChannel = 10;  binding.midiNumber = controller;
            auto* params = new juce::DynamicObject();
            params->setProperty("action", action);
            params->setProperty("binding", model::toVar(binding));
            session.commands().run("controls.set", juce::var(params));
        }
        if (auto* setup = scene.page<SetupPage>(Page::Setup)) setup->showSubTab(SetupPage::SubTab::Midi);
    } else if (name == "snapshots") {
        session.snapshots().create(session.model(), "Before soundcheck", false);
        // Then a pedal change moved a bar, a cue added, the click turned up and every part of all three songs lifted: more lines
        // than the dialog lists, so its last line says how many more.
        juce::String deviceId;
        if (const auto markerId = changeAt(session, scene.songIdAt(0), "Guitar 1", 9, deviceId); markerId.isNotEmpty())
            session.commands().run("marker.set", model::jsonObject({{"songId", scene.songIdAt(0)}, {"deviceId", deviceId},
                                                                    {"markerId", markerId}, {"bar", 10}}));
        addWatchMe(session, scene.songIdAt(0));
        addWatchMe(session, scene.songIdAt(1));
        addWatchMe(session, scene.songIdAt(2));
        session.commands().run("band.setLevel", model::jsonObject({{"which", "click"}, {"db", session.model().band().clickDb + 2.0f}}));
        for (int i = 0; i < 3; ++i)
            if (const auto* song = session.model().song(scene.songIdAt(i)))
                for (const auto& part : song->parts)
                    session.commands().run("part.set", model::jsonObject({{"songId", song->id}, {"partId", part.id}, {"gainDb", 2}}));
        shell.openSnapshotsDialog();
    } else if (name == "mixer-song" || name == "mixer-output") {
        // The first song mixed as the mockup's frames have it, played to bar 10 with the meters moving, as the mockup's
        // "Playing · bar 10.1".
        const auto songId = scene.songIdAt(0);
        const auto* rig = session.model().rig(session.rigId());
        const auto outputId = rig != nullptr && !rig->outputs.empty() ? rig->outputs[0].id : juce::String();
        dressSongMix(session, songId);
        if (name == "mixer-output") dressOutputMix(session, outputId);
        scene.measureLevels();                                    // the loudest moments
        if (auto* mixer = scene.page<MixerPage>(Page::Mixer)) {
            mixer->openSong(songId);
            if (name == "mixer-output") mixer->chooseOutput(outputId);
            session.playSong(songId, 0.0);
            for (int i = 0; i < 6000 && session.now().seconds < 21.0; ++i) session.advanceWithoutDevice(kRate / 100);
            pump();
            mixer->tickMeters(juce::Time::getMillisecondCounterHiRes());   // the role meters on the output start from here
            playMeterFrames(session, [mixer](double now) { mixer->tickMeters(now); });
        }
    } else if (name == "effects-window") {
        // As the mockup's #chain: the first song's Keys with an EQ (cut below 60 Hz, a dip at 310 Hz, a lift above 6 kHz) and a
        // Dynamic, played to bar 10, the window open on them with the add menu out, and the song not yet prepared again for
        // the show (the preparer held, and a last touch just made).
        const auto songId = scene.songIdAt(0);
        const auto keys = partNamed(session, songId, "Keys");
        auto* mixer = scene.page<MixerPage>(Page::Mixer);
        if (mixer == nullptr || keys.isEmpty()) return {};
        session.preparer().setPaused(true);
        dressSongMix(session, songId);
        mixer->openSong(songId);
        const app::EffectsTarget target{songId, keys, {}, {}};
        session.addEffect(target, "eq");
        session.addEffect(target, "dynamic");
        session.setEffect(target, 0, withKeysEq(session.effects(target)[0]), "screens eq");
        const auto dynamic = [&](double thresholdDb) {
            auto effect = session.effects(target)[1];
            effect.values.set("threshold", thresholdDb);
            return effect;
        };
        session.setEffect(target, 1, dynamic(-24.0), "screens dynamic");
        session.playSong(songId, 0.0);
        for (int i = 0; i < 6000 && session.now().seconds < 21.0; ++i) session.advanceWithoutDevice(kRate / 100);
        pump();
        shell.openEffects(target, -1);
        auto& window = shell.effectsWindow();
        shell.setBounds(0, 0, width, height);
        session.setEffect(target, 1, dynamic(-25.0), "screens last");   // the last touch: changed, to be prepared again
        pump();
        for (int i = 0; i < 90; ++i) {                            // three seconds of the music: the spectrum, the history
            session.advanceWithoutDevice(kRate / 30);
            window.step(1.0 / 30.0);
            mixer->tickMeters(juce::Time::getMillisecondCounterHiRes());
        }
        return shot();
    } else if (name == "remote-setup") {
        // The server on, one phone waiting and two let in, as the mockup's screen 14 has it. Nothing is started: the page
        // draws a state it is given, on an address that is not this computer's.
        auto& access = scene.remoteAccess();
        const auto pin = access.pin();
        for (const auto* phone : {"Sam's iPhone", "Drummer's iPhone", "Guitarist's Pixel"})
            access.join(phone, pin, juce::String("10.0.0.") + juce::String(access.phones().size() + 2), 0.0);
        const auto phones = access.phones();
        if (phones.size() == 3) {
            access.decide(phones[1].id, app::PhoneAccess::FullControl, true);
            access.decide(phones[2].id, app::PhoneAccess::ViewOnly, true);
            access.setConnected(phones[1].id, true);
        }
        scene.settle();
        if (auto* setup = scene.page<SetupPage>(Page::Setup)) {
            setup->showSubTab(SetupPage::SubTab::Remote);
            if (auto* remote = setup->remote()) {
                auto state = makeRemoteSetupState(session, access, scene.remoteServer());
                state.on = true;  state.address = "http://192.168.1.20:8080";  state.pin = "4719";  state.problem = {};
                remote->showState(state);
            }
        }
        return shot();
    } else if (name == "routing") {
        scene.page<juce::Component>(Page::Setup);
    } else if (name == "cue-voice") {
        // As the mockup's screen 13 has it: the voice made, Solo announced by the band's own recording, Breakdown by none.
        session.setVoiceRunner([](const app::VoiceCommand& command) { return writeTone(command.wavFile, 0.6, 330.0); });
        const auto done = std::make_shared<bool>(false);
        if (session.makeVoice(false, {}, [done](juce::Result) { *done = true; }).wasOk())
            for (int i = 0; i < 300 && !*done; ++i) pump();
        if (const auto recording = scene.folder().getChildFile("Solo, Jeff.wav"); writeTone(recording, 0.8, 440.0))
            if (const auto path = session.importCueClip(recording); path.isNotEmpty())
                session.commands().run("sectionType.set", model::jsonObject({{"typeId", "solo"}, {"cueClip", path}}));
        session.commands().run("sectionType.set", model::jsonObject({{"typeId", "breakdown"}, {"cueClip", ""}}));
        if (auto* setup = scene.page<SetupPage>(Page::Setup)) setup->showSubTab(SetupPage::SubTab::CueVoice);
    } else {
        return {};
    }
    scene.settle();
    return shot();
}

}  // namespace

juce::StringArray screenNames() {
    return {"main-view", "stage-playing", "stage-looping", "stage-between", "stage-loading", "preshow-check", "interrupted",
            "welcome", "new-computer", "setlists", "new-song", "song-editor", "mixer-song", "mixer-output", "routing",
            "midi-setup", "cue-voice", "snapshots", "remote-setup", "effects-window"};
}

juce::Image renderScreen(const juce::String& name, int width, int height) {
    if (!screenNames().contains(name)) return {};
    return render(name, width, height);
}

juce::Result renderScreens(const juce::File& folder, int width, int height) {
    if (!folder.createDirectory()) return juce::Result::fail("Couldn't make the folder " + folder.getFullPathName() + ".");
    for (const auto& name : screenNames()) {
        const auto image = renderScreen(name, width, height);
        if (!image.isValid()) return juce::Result::fail("The screen \"" + name + "\" couldn't be drawn.");
        const auto target = folder.getChildFile(name + ".png");
        const auto part = folder.getChildFile(name + ".png.part");
        part.deleteFile();
        bool written = false;
        {
            juce::FileOutputStream out(part);
            written = out.openedOk() && juce::PNGImageFormat().writeImageToStream(image, out);
            out.flush();
            written = written && out.getStatus().wasOk();
        }                                                         // closed here, so it can be moved or deleted
        if (!written) {
            part.deleteFile();
            return juce::Result::fail("Couldn't write " + part.getFullPathName() + ".");
        }
        if (!part.moveFileTo(target)) {
            part.deleteFile();
            return juce::Result::fail("Couldn't move " + part.getFullPathName() + " into place.");
        }
    }
    return juce::Result::ok();
}

}  // namespace sqlive::ui
