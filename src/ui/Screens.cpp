#include "ui/Screens.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>

#include <juce_audio_formats/juce_audio_formats.h>

#include "app/AudioHost.h"
#include "app/ControlInput.h"
#include "app/MidiPorts.h"
#include "app/Session.h"
#include "app/Settings.h"
#include "app/ShowController.h"
#include "model/ActionNames.h"
#include "model/DemoBand.h"
#include "model/DocumentsJson.h"
#include "model/JsonObject.h"
#include "model/Snapshots.h"
#include "ui/AppShell.h"
#include "ui/MixerPage.h"
#include "ui/NewComputerPage.h"
#include "ui/Pages.h"
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
        // As the app makes the demo band: with its pedalboards and their changes, which Undo can't take away.
        if (model::addDemoMidi(session_->commands()).failed() || !bindPorts()) return;
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
        shell_ = std::make_unique<AppShell>(*session_, host_.get());
        shell_->setShowController(controller_.get());
        plugInPages(*shell_, *session_, host_.get(), controller_.get(), input_.get());
        settle();
    }

    ~Scene() {
        shell_.reset();
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
        for (int i = 0; i < 2000 && session_->now().seconds < seconds; ++i) session_->advanceWithoutDevice(kRate / 100);
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

    juce::File folder_;
    std::unique_ptr<app::Settings> settings_;
    std::unique_ptr<app::Session> session_;
    std::unique_ptr<app::MidiPorts> midi_;
    std::unique_ptr<app::AudioHost> host_;
    std::unique_ptr<app::ShowController> controller_;
    std::unique_ptr<app::ControlInput> input_;
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

// The song's sections, as the mockup's screen 9 has them (on the demo's 16 bars), and its arrangement "Long ending" (the
// Chorus once more before the Outro, still to be put in by the editor). The id of the arrangement and of the last Chorus;
// "" when a command was refused.
std::pair<juce::String, juce::String> addMockupSections(app::Session& session, const juce::String& songId) {
    struct Start { const char* name; int bar; };
    const Start starts[] = {{"Intro", 1}, {"Verse 1", 3}, {"Chorus", 5}, {"Verse 2", 7}, {"Chorus", 9}, {"Bridge", 11}, {"Outro", 14}};
    juce::StringArray ids;
    for (const auto& start : starts) {
        const auto made = session.commands().run(
            "section.add", model::jsonObject({{"songId", songId}, {"name", start.name}, {"startBar", start.bar}}));
        if (!made.ok) return {};
        ids.add(made.value.toString());
    }
    juce::Array<juce::var> steps;
    for (const int i : {0, 1, 2, 3, 4, 5, 4, 6}) steps.add(ids[i]);
    const auto made = session.commands().run(
        "arrangement.add", model::jsonObject({{"songId", songId}, {"name", "Long ending"}, {"steps", steps}}));
    return made.ok ? std::pair{made.value.toString(), ids[4]} : std::pair<juce::String, juce::String>{};
}

// As the mockup's screen 9 has them: "Watch me" said in the second Chorus, between the automatic cues, and the second part
// faded in over the first Chorus (the mockup's Gtr FX lane).
void addMockupCuesAndAutomation(app::Session& session, const juce::String& songId) {
    session.commands().run("cue.add", model::jsonObject({{"songId", songId}, {"bar", 10}, {"beat", 1.0},
                                                         {"clip", "cues/built-in/Watch me.wav"}, {"text", "Watch me"}}));
    const auto* song = session.model().song(songId);
    if (song == nullptr || song->parts.size() < 2) return;
    const auto chorus = std::find_if(song->sections.begin(), song->sections.end(), [](const auto& s) { return s.startBar == 5; });
    if (chorus == song->sections.end()) return;
    session.commands().run("automation.fadeIn",
                           model::jsonObject({{"songId", songId}, {"target", song->parts[1].id}, {"sectionId", chorus->id}}));
}

// The mockup's screen 3 has the next song in eight sections; on the second demo song's 12 bars.
void addBetweenSections(app::Session& session, const juce::String& songId) {
    struct Start { const char* name; int bar; };
    const Start starts[] = {{"Intro", 1}, {"Verse 1", 2}, {"Pre-chorus", 4}, {"Chorus", 5},
                            {"Verse 2", 7}, {"Chorus", 8}, {"Solo", 10}, {"Outro", 11}};
    for (const auto& start : starts)
        session.commands().run("section.add", model::jsonObject({{"songId", songId}, {"name", start.name}, {"startBar", start.bar}}));
}

juce::Image render(const juce::String& name, int width, int height) {
    Scene scene(name == "interrupted" || name == "new-computer" || name == "routing");
    if (!scene.ok()) return {};
    auto& session = scene.session();
    auto& shell = scene.shell();

    if (name == "main-view" || name == "stage-playing" || name == "stage-looping") {
        // The first song with the mockup's sections and cues, played into its first Chorus; the second Chorus chosen to
        // come next with ↓ (or, looping, the Chorus looped with L).
        addMockupSections(session, scene.songIdAt(0));
        addMockupCuesAndAutomation(session, scene.songIdAt(0));
        scene.settle();
        session.enterShow();
        scene.wantStageView(name != "main-view");
        const auto* band = &session.model().band();
        session.setLevel({app::LevelTarget::Kind::BandClick, {}, {}, {}, {}}, band->clickDb + 2.0f, "screens");
        scene.playUntil(10.0);
        session.perform(name == "stage-looping" ? Action::LoopSection : Action::NextSectionLater);
        session.advanceWithoutDevice(kRate / 100);
        pump();
    } else if (name == "stage-between" || name == "stage-loading") {
        addBetweenSections(session, scene.songIdAt(1));
        scene.settle();
        session.enterShow();
        scene.wantStageView(true);
        scene.playThrough();
        session.chooseNext(1);
        scene.settle();
        if (name == "stage-loading") {
            session.cache().setPaused(true);
            session.cache().forget(session.now().nextSongId);
            pump();
            auto image = snapshot(shell, width, height);
            session.cache().setPaused(false);
            scene.settle();
            return image;
        }
    } else if (name == "preshow-check") {
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
        const auto arrangementId = addMockupSections(session, scene.songIdAt(0)).first;
        const auto* list = session.model().setlist(session.setlistId());
        if (arrangementId.isNotEmpty() && list != nullptr && list->entries.size() > 1) {
            const auto first = list->entries[0].id, second = list->entries[1].id, setlistId = session.setlistId();
            session.commands().run("setlist.set", model::jsonObject({{"setlistId", setlistId}, {"playMode", "continuous"}}));
            session.commands().run("setlist.setArrangement",
                                   model::jsonObject({{"setlistId", setlistId}, {"entryId", first}, {"arrangementId", arrangementId}}));
            session.commands().run("setlist.setTransition", model::jsonObject({{"setlistId", setlistId}, {"entryId", first},
                                                                               {"kind", "crossfade"}, {"seconds", 4}}));
            session.commands().run("setlist.setTransition", model::jsonObject({{"setlistId", setlistId}, {"entryId", second},
                                                                               {"kind", "gap"}, {"seconds", 4}}));
        }
        scene.page<SetlistsPage>(Page::Setlists);
    } else if (name == "new-song") {
        if (auto* setlists = scene.page<SetlistsPage>(Page::Setlists); setlists != nullptr && setlists->onNewSong)
            setlists->onNewSong(stemsFor(scene));
    } else if (name == "song-editor") {
        const auto [arrangementId, chorusId] = addMockupSections(session, scene.songIdAt(0));
        addMockupCuesAndAutomation(session, scene.songIdAt(0));
        if (auto* setlists = scene.page<SetlistsPage>(Page::Setlists); setlists != nullptr && setlists->onOpenSong)
            setlists->onOpenSong(scene.songIdAt(0));              // as a double-click does: the editor, and the Mixer's song
        if (auto* editor = scene.page<SongEditorPage>(Page::SongEditor)) {
            editor->arrangementRow().choose(arrangementId);       // "Long ending", with its last Chorus just put in again
            editor->arrangementRow().insertSection(chorusId, 7);
            editor->setCursorSeconds(12.0);
            juce::String deviceId;                                // Guitar 1's Crunch selected, so the inspector shows
            const auto markerId = changeAt(session, scene.songIdAt(0), "Guitar 1", 9, deviceId);
            if (markerId.isNotEmpty()) editor->timeline().select({EditorSelection::Kind::Marker, deviceId, markerId});
        }
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
        shell.openSnapshotsDialog();
    } else if (name == "mixer-song" || name == "mixer-output") {
        if (auto* mixer = scene.page<MixerPage>(Page::Mixer)) {
            mixer->openSong(scene.songIdAt(0));
            const auto* rig = session.model().rig(session.rigId());
            if (name == "mixer-output" && rig != nullptr && !rig->outputs.empty()) mixer->chooseOutput(rig->outputs[0].id);
        }
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
    return snapshot(shell, width, height);
}

}  // namespace

juce::StringArray screenNames() {
    return {"main-view", "stage-playing", "stage-looping", "stage-between", "stage-loading", "preshow-check", "interrupted",
            "welcome", "new-computer", "setlists", "new-song", "song-editor", "mixer-song", "mixer-output", "routing",
            "midi-setup", "cue-voice", "snapshots"};
}

juce::Image renderScreen(const juce::String& name, int width, int height) {
    if (!screenNames().contains(name)) return {};
    return render(name, width, height);
}

juce::Result renderScreens(const juce::File& folder) {
    if (!folder.createDirectory()) return juce::Result::fail("Couldn't make the folder " + folder.getFullPathName() + ".");
    for (const auto& name : screenNames()) {
        const auto image = renderScreen(name);
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
