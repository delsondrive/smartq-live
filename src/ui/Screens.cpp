#include "ui/Screens.h"

#include <memory>

#include "app/AudioHost.h"
#include "app/Session.h"
#include "app/Settings.h"
#include "app/ShowController.h"
#include "model/DemoBand.h"
#include "ui/AppShell.h"
#include "ui/MixerPage.h"
#include "ui/NewComputerPage.h"
#include "ui/Pages.h"
#include "ui/SetlistsPage.h"
#include "ui/SongEditorPage.h"
#include "ui/WelcomePage.h"

namespace sqlive::ui {

namespace {

using app::Action;
using app::Mode;

constexpr int kRate = 48000;

// Runs the message thread's pending work: change messages, async updates and timers the views wait on.
void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(30); }

// A demo band in a temp folder of its own, with its own settings file there, a session on it with no real device, and the
// shell wired as MainWindow wires it. An AudioHost only when asked for, and then driven only through its test hooks.
// Everything goes, the temp folder last, when this does.
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
        if (withHost) host_ = std::make_unique<app::AudioHost>(*session_);
        controller_ = std::make_unique<app::ShowController>(*session_);
        shell_ = std::make_unique<AppShell>(*session_, host_.get());
        shell_->setShowController(controller_.get());
        plugInPages(*shell_, *session_, host_.get(), controller_.get());
        settle();
    }

    ~Scene() {
        shell_.reset();
        controller_.reset();
        host_.reset();
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
    juce::File folder_;
    std::unique_ptr<app::Settings> settings_;
    std::unique_ptr<app::Session> session_;
    std::unique_ptr<app::AudioHost> host_;
    std::unique_ptr<app::ShowController> controller_;
    std::unique_ptr<AppShell> shell_;
};

juce::Image snapshot(juce::Component& component, int width, int height) {
    component.setBounds(0, 0, width, height);
    pump();                                                       // what a resize posted
    return component.createComponentSnapshot(component.getLocalBounds());
}

// The stems a band would drop for a new song: three the app recognises, one it can't place, and a click it leaves out.
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
    return files;
}

juce::Image render(const juce::String& name, int width, int height) {
    Scene scene(name == "interrupted" || name == "new-computer" || name == "routing");
    if (!scene.ok()) return {};
    auto& session = scene.session();
    auto& shell = scene.shell();

    if (name == "main-view" || name == "stage-playing") {
        session.enterShow();
        scene.wantStageView(name == "stage-playing");
        const auto* band = &session.model().band();
        session.setLevel({app::LevelTarget::Kind::BandClick, {}, {}, {}, {}}, band->clickDb + 2.0f, "screens");
        scene.playUntil(12.0);
    } else if (name == "stage-between" || name == "stage-loading") {
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
        scene.page<SetlistsPage>(Page::Setlists);
    } else if (name == "new-song") {
        if (auto* setlists = scene.page<SetlistsPage>(Page::Setlists); setlists != nullptr && setlists->onNewSong)
            setlists->onNewSong(stemsFor(scene));
    } else if (name == "song-editor") {
        if (auto* setlists = scene.page<SetlistsPage>(Page::Setlists); setlists != nullptr && setlists->onOpenSong)
            setlists->onOpenSong(scene.songIdAt(0));              // as a double-click does: the editor, and the Mixer's song
        if (auto* editor = scene.page<SongEditorPage>(Page::SongEditor)) editor->setCursorSeconds(12.0);
    } else if (name == "mixer-song" || name == "mixer-output") {
        if (auto* mixer = scene.page<MixerPage>(Page::Mixer)) {
            mixer->openSong(scene.songIdAt(0));
            const auto* rig = session.model().rig(session.rigId());
            if (name == "mixer-output" && rig != nullptr && !rig->outputs.empty()) mixer->chooseOutput(rig->outputs[0].id);
        }
    } else if (name == "routing") {
        scene.page<juce::Component>(Page::Setup);
    } else {
        return {};
    }
    scene.settle();
    return snapshot(shell, width, height);
}

}  // namespace

juce::StringArray screenNames() {
    return {"main-view", "stage-playing", "stage-between", "stage-loading", "preshow-check", "interrupted", "welcome",
            "new-computer", "setlists", "new-song", "song-editor", "mixer-song", "mixer-output", "routing"};
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
