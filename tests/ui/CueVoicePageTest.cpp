#include <algorithm>
#include <atomic>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/PreShowCheck.h"
#include "model/MeaningColours.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "tests/support/Signals.h"
#include "ui/CueVoicePage.h"
#include "ui/EditorInspector.h"
#include "ui/MixerPanel.h"
#include "ui/RoutingGrid.h"
#include "ui/SetupPage.h"
#include "ui/SongEditorPage.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;
using app::LevelTarget;

namespace {

void pumpFor(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

juce::MouseEvent eventOn(juce::Component& component, juce::Point<float> at) {
    const auto now = juce::Time::getCurrentTime();
    const juce::ModifierKeys left(juce::ModifierKeys::leftButtonModifier);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, left, juce::MouseInputSource::defaultPressure,
                            0.0f, 0.0f, 0.0f, 0.0f, &component, &component, now, at, now, 1, false);
}

// Until the session has finished making the voice (and called onDone), at most 10 s.
void waitUntilMade(app::Session& s) {
    for (int i = 0; i < 500 && s.makingVoice(); ++i) pumpFor(20);
}

// A runner that never starts PowerShell: it writes something where the voice would be, and counts.
app::VoiceMaker::Runner countingRunner(std::shared_ptr<std::atomic<int>> ran, int sleepMs = 0) {
    return [ran, sleepMs](const app::VoiceCommand& c) {
        if (sleepMs > 0) juce::Thread::sleep(sleepMs);
        ++*ran;
        return c.wavFile.replaceWithText("not really a wav");
    };
}

int rowOf(const CueVoiceState& state, const juce::String& typeId) {
    for (int i = 0; i < (int) state.rows.size(); ++i)
        if (state.rows[(size_t) i].typeId == typeId) return i;
    return -1;
}

const app::CheckItem* findItem(const std::vector<app::CheckItem>& items, const char* id) {
    for (auto& i : items) if (i.id == id) return &i;
    return nullptr;
}

}  // namespace

TEST_CASE("the cue voice page lists section types with what announces them", "[cuevoice]") {
    DemoSession d; auto& s = *d.session;
    auto state = makeCueVoiceState(s);
    CHECK(state.lead == "1 bar before");  CHECK(state.level == "-9 dB");  CHECK(state.heardIn == "In-ears mixes only");
    REQUIRE(state.rows.size() == 9);
    CHECK(state.rows[3].name == "Chorus");  CHECK(state.rows[3].cue == juce::String::fromUTF8("“Chorus”"));
    CHECK(state.rows[3].clipMissing);  CHECK_FALSE(state.voiceMade);            // the voice hasn't been made on this band folder yet
    writeSine(s.folder().root().getChildFile("cues/Solo Jeff.wav"), 48000.0, 1, 0.5, 300.0, 0.3f);
    s.commands().run("sectionType.set", params({{"typeId", state.rows[5].typeId}, {"cueClip", "cues/Solo Jeff.wav"}}));
    s.commands().run("sectionType.set", params({{"typeId", state.rows[7].typeId}, {"cueClip", ""}}));
    auto after = makeCueVoiceState(s);
    CHECK(after.rows[5].cue == juce::String::fromUTF8("Your recording · “Solo Jeff”"));  CHECK_FALSE(after.rows[5].clipMissing);
    CHECK(after.rows[7].cue == "No cue");
    auto items = app::runPreShowCheck(s, {true, "Test", 48000.0, 512, 2, {}});
    CHECK(std::none_of(items.begin(), items.end(), [](auto& i) { return i.id == "cues"; }));   // no song has sections yet: nothing to warn about
}

TEST_CASE("the voice is made by the session, never during a show, and an import never writes over a clip", "[cuevoice]") {
    DemoSession d; auto& s = *d.session;
    int ran = 0;
    s.setVoiceRunner([&](const app::VoiceCommand& c) { ++ran; return c.wavFile.replaceWithText("not really a wav"); });   // never PowerShell
    bool done = false;  juce::Result result = juce::Result::fail("not done");
    REQUIRE(s.makeVoice(false, {}, [&](juce::Result r) { done = true; result = r; }).wasOk());
    for (int i = 0; i < 500 && !done; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    REQUIRE(done);  CHECK(result.wasOk());
    CHECK(ran == 13);                                              // nine section types and four phrases
    CHECK(makeCueVoiceState(s).voiceMade);
    CHECK(s.folder().root().getChildFile("cues/built-in/1-2-3-4.wav").existsAsFile());
    s.enterShow();
    CHECK(s.makeVoice(true, {}, {}).failed());                     // Show mode never writes the band folder
    CHECK(s.leaveShow());
    auto source = d.dir.dir().getChildFile("Solo Jeff.wav");  writeSine(source, 48000.0, 1, 0.5, 300.0, 0.3f);
    writeSine(s.folder().root().getChildFile("cues/Solo Jeff.wav"), 48000.0, 1, 0.25, 300.0, 0.3f);   // a bandmate's clip of that name
    CHECK(s.importCueClip(source) == "cues/Solo Jeff 2.wav");
    CHECK(s.importClickSound(source) == "clicks/Solo Jeff.wav");
    CHECK(s.cues().clickSoundsInFolder().contains("clicks/Solo Jeff.wav"));
}

TEST_CASE("cues have a strip in the mixer and a row in the routing grid", "[cuevoice]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto mixer = makeMixerState(s, song, {});
    CHECK(mixer.strips[1].name == "Cues");  CHECK(mixer.strips[1].inEarsOnly);  CHECK(mixer.strips[1].group == 0);
    CHECK(mixer.strips[1].target.kind == LevelTarget::Kind::BandCues);
    auto routing = makeRoutingState(s, nullptr);
    CHECK(routing.rows[1].name == "Cues");
    s.commands().run("output.add", params({{"rigId", s.rigId()}, {"name", "PA"}, {"firstChannel", 3}, {"stereo", true}, {"label", "foh"}}));
    CHECK(makeRoutingState(s, nullptr).rows[1].cells[1].whyNot == "Cues can only go to in-ear outputs.");
}

TEST_CASE("the Cues strip sets the band's cue level, and each in-ear mix has its own", "[cuevoice]") {
    DemoSession d; auto& s = *d.session; auto song = d.setlist().entries[0].songId;
    auto here = makeMixerState(s, song, {});
    REQUIRE(here.strips.size() == 5);                             // Click, Cues and the song's three parts
    CHECK(here.strips[1].value == "-9.0");  CHECK_FALSE(here.strips[1].canMute);
    s.setLevel(here.strips[1].target, -12.0f, "g");
    CHECK(s.model().band().cuesDb == -12.0f);
    CHECK(makeCueVoiceState(s).level == "-12 dB");
    const auto output = s.model().rig(s.rigId())->outputs[0].id;
    auto mix = makeMixerState(s, song, output);
    CHECK(mix.strips[1].name == "Cues");  CHECK(mix.strips[1].target.kind == LevelTarget::Kind::Mix);  CHECK(mix.strips[1].canMute);
    s.setLevel(mix.strips[1].target, std::nullopt, "h");            // no cues in that mix
    CHECK(makeMixerState(s, song, output).strips[1].value == "off");
    CHECK(makeCueVoiceState(s).heardIn == "No output yet");
}

TEST_CASE("the voice's state follows the clips on disk, and what Make it again would replace", "[cuevoice]") {
    DemoSession d; auto& s = *d.session;
    auto ran = std::make_shared<std::atomic<int>>(0);
    s.setVoiceRunner(countingRunner(ran));
    ui::CueVoicePage page(s);
    CHECK(page.makeButtonText() == "Make the voice");
    CHECK(page.state().madeBuiltIns.isEmpty());
    CHECK(page.state().otherClips.joinIntoString(", ")
          == juce::String::fromUTF8("“Watch me”, “Last time”, “Two bars to stop”, “1-2-3-4”"));
    page.makeVoice();                                             // nothing made yet: no question, it is made
    CHECK(page.making());
    waitUntilMade(s);
    CHECK(*ran == 13);
    CHECK_FALSE(page.making());  CHECK(page.message() == "The voice is made.");
    CHECK(page.state().voiceMade);  CHECK(page.makeButtonText() == "Make it again");
    CHECK(page.state().madeBuiltIns.size() == 13);
    const auto question = page.makeAgainQuestion();
    CHECK(question.contains(juce::String::fromUTF8("“Chorus”")));  CHECK(question.contains(juce::String::fromUTF8("“1-2-3-4”")));
    CHECK(question.contains("everyone in the band"));

    SECTION("Only make the missing ones") {
        s.folder().root().getChildFile("cues/built-in/Verse.wav").deleteFile();   // a bandmate's sync took it away
        page.refreshNow();
        CHECK_FALSE(page.state().voiceMade);
        CHECK(page.state().rows[1].clipMissing);
        *ran = 0;
        page.makeVoice(false);
        waitUntilMade(s);
        CHECK(*ran == 1);                                         // only the Verse
        CHECK(page.state().voiceMade);
    }
    SECTION("Replace them") {
        *ran = 0;
        page.makeVoice(true);
        waitUntilMade(s);
        CHECK(*ran == 13);
    }
    SECTION("a type that names no built-in clip is left alone, and an unused recording is an other clip") {
        writeSine(s.folder().root().getChildFile("cues/Count off.wav"), 48000.0, 1, 0.25, 300.0, 0.3f);
        s.commands().run("sectionType.set", params({{"typeId", "chorus"}, {"cueClip", ""}}));
        page.refreshNow();
        CHECK(page.state().otherClips.contains(juce::String::fromUTF8("“Count off”")));
        CHECK_FALSE(page.state().madeBuiltIns.contains("Chorus"));
        CHECK_FALSE(page.makeAgainQuestion().contains(juce::String::fromUTF8("“Chorus”")));
    }
}

TEST_CASE("a voice this computer can't make is said plainly", "[cuevoice]") {
    DemoSession d; auto& s = *d.session;
    s.setVoiceRunner([](const app::VoiceCommand&) { return false; });   // as when the system has no speech
    ui::CueVoicePage page(s);
    page.makeVoice();
    waitUntilMade(s);
    CHECK(page.message() == "This computer couldn't make the voice. You can still record your own clips.");
    CHECK(page.messageIsProblem());
    CHECK_FALSE(page.state().voiceMade);
    CHECK_FALSE(s.folder().root().getChildFile("cues/built-in").exists());   // nothing half-made in the band folder
    s.enterShow();
    page.refreshNow();
    page.makeVoice(false);                                        // the button is off in Show mode; nothing happens
    CHECK_FALSE(s.makingVoice());
}

TEST_CASE("entering Show mode stops the voice being made", "[cuevoice]") {
    DemoSession d; auto& s = *d.session;
    auto ran = std::make_shared<std::atomic<int>>(0);
    s.setVoiceRunner(countingRunner(ran, 40));
    auto result = std::make_shared<juce::Result>(juce::Result::ok());
    auto done = std::make_shared<bool>(false);
    REQUIRE(s.makeVoice(false, {}, [result, done](juce::Result r) { *result = r; *done = true; }).wasOk());
    CHECK(s.makeVoice(false, {}, {}).failed());                   // one at a time
    while (*ran < 2) juce::Thread::sleep(5);
    s.enterShow();
    for (int i = 0; i < 500 && !*done; ++i) pumpFor(20);
    REQUIRE(*done);
    CHECK(result->failed());
    CHECK(result->getErrorMessage() == "Show mode never changes the band folder. Make the voice after the show.");
    CHECK(*ran < 13);
    CHECK_FALSE(s.makingVoice());
}

TEST_CASE("closing the band while the voice is being made stops it safely", "[cuevoice]") {
    DemoSession d;
    auto ran = std::make_shared<std::atomic<int>>(0);
    d.session->setVoiceRunner(countingRunner(ran, 40));
    auto progressed = std::make_shared<int>(0);
    auto done = std::make_shared<bool>(false);
    REQUIRE(d.session->makeVoice(false, [progressed](double) { ++*progressed; }, [done](juce::Result) { *done = true; }).wasOk());
    while (*ran < 2) juce::Thread::sleep(5);
    const auto builtIn = d.session->folder().root().getChildFile("cues/built-in");
    d.session.reset();                                            // the band closes: it waits for the clip being made, at most
    const auto ranAtClose = ran->load();
    const int progressAtClose = *progressed;
    pumpFor(300);                                                 // what the job posted reaches no session
    CHECK_FALSE(*done);
    CHECK(*progressed == progressAtClose);
    CHECK(ranAtClose < 13);  CHECK(ran->load() == ranAtClose);    // nothing more is said
    for (const auto& file : builtIn.findChildFiles(juce::File::findFiles, false))   // whole clips, nothing half-written
        CHECK(file.getFileExtension() == ".wav");
}

TEST_CASE("Hear all plays each type's clip in turn, and nothing while a song plays", "[cuevoice]") {
    DemoSession d; auto& s = *d.session;
    for (const char* name : {"Intro", "Verse"})
        writeSine(s.folder().root().getChildFile(juce::String("cues/built-in/") + name + ".wav"), 48000.0, 1, 0.2, 300.0, 0.3f);
    ui::CueVoicePage page(s);
    CHECK(page.hearAll());
    CHECK(page.hearing());
    s.perform(app::Action::Start);
    s.advanceWithoutDevice(4800);
    REQUIRE(s.now().playing);
    CHECK_FALSE(page.hearAll());
    CHECK(page.message() == "Cues can only be heard while no song is playing.");
    CHECK_FALSE(page.hearing());
    page.hear(0);
    CHECK_FALSE(page.hearing());
    CHECK_FALSE(s.hearCue("cues/built-in/Intro.wav"));
}

TEST_CASE("a type's CUE offers its built-in clip, every recording, an import and no cue", "[cuevoice]") {
    DemoSession d; auto& s = *d.session;
    writeSine(s.folder().root().getChildFile("cues/Count off.wav"), 48000.0, 1, 0.25, 300.0, 0.3f);
    ui::CueVoicePage page(s);
    const int chorus = rowOf(page.state(), "chorus");
    REQUIRE(chorus >= 0);
    auto choice = page.cueChoice(chorus);
    CHECK(choice.items[0] == juce::String::fromUTF8("“Chorus”"));  CHECK(choice.selected == 0);
    CHECK(choice.items.contains(juce::String::fromUTF8("Your recording · “Count off”")));
    CHECK(choice.items[choice.importItem] == juce::String::fromUTF8("Import a recording…"));
    CHECK(choice.items[choice.noCueItem] == "No cue");
    CHECK(choice.noCueItem == choice.items.size() - 1);

    auto source = d.dir.dir().getChildFile("Solo, Jeff.wav");  writeSine(source, 44100.0, 2, 0.5, 300.0, 0.3f);
    page.importRecording(source, "chorus");                       // what "Import a recording…" does with the file chosen
    auto row = page.state().rows[(size_t) chorus];
    CHECK(row.clip == "cues/Solo, Jeff.wav");  CHECK_FALSE(row.clipMissing);
    CHECK(row.cue == juce::String::fromUTF8("Your recording · “Solo, Jeff”"));
    CHECK(page.cueChoice(chorus).items[page.cueChoice(chorus).selected] == row.cue);
    CHECK(s.commands().canUndo());

    page.chooseCue(chorus, page.cueChoice(chorus).noCueItem);
    CHECK(page.state().rows[(size_t) chorus].cue == "No cue");
    CHECK_FALSE(page.state().rows[(size_t) chorus].hasClip);
    page.chooseCue(chorus, 0);
    CHECK(page.state().rows[(size_t) chorus].clip == "cues/built-in/Chorus.wav");

    page.importRecording(d.dir.dir().getChildFile("missing.wav"), "chorus");
    CHECK(page.messageIsProblem());  CHECK(page.message().startsWith("missing.wav couldn't be brought in"));
}

TEST_CASE("section types are renamed, coloured and added from the page", "[cuevoice]") {
    DemoSession d; auto& s = *d.session;
    ui::CueVoicePage page(s);
    const int chorus = rowOf(page.state(), "chorus");
    page.renameType(chorus, "Hook");                              // its built-in cue follows its name
    CHECK(page.state().rows[(size_t) chorus].name == "Hook");
    CHECK(page.state().rows[(size_t) chorus].clip == "cues/built-in/Hook.wav");
    CHECK(page.state().rows[(size_t) chorus].cue == juce::String::fromUTF8("“Hook”"));
    page.renameType(chorus, "Verse");
    CHECK(page.message() == "There is already a section type called Verse.");  CHECK(page.messageIsProblem());

    for (const auto& [name, colour] : sectionTypeColours())
        CHECK(std::none_of(model::meaning::all.begin(), model::meaning::all.end(), [&](juce::Colour c) { return c == colour; }));
    page.setTypeColour(chorus, sectionTypeColours()[1].second);
    CHECK(page.state().rows[(size_t) chorus].colour == sectionTypeColours()[1].second);
    page.setTypeColour(chorus, model::meaning::mint);             // never offered; refused with the command's sentence
    CHECK(page.message() == "That colour is kept for playing now, next, needs a look and broken. Pick another.");

    page.addType("Tag");
    REQUIRE(page.state().rows.size() == 10);
    const auto& tag = page.state().rows.back();
    CHECK(tag.name == "Tag");  CHECK_FALSE(tag.builtIn);  CHECK(tag.clip == "cues/built-in/Tag.wav");  CHECK(tag.clipMissing);
    CHECK(page.state().rows[0].builtIn);
    s.commands().undo();                                          // the type and its cue were one step
    page.refreshNow();
    CHECK(page.state().rows.size() == 9);

    page.chooseLead(2);
    CHECK(page.state().lead == "2 bars before");  CHECK(s.model().band().cueLead == model::CueLead::TwoBars);
    page.setLevel(-6.0f);
    CHECK(page.state().level == "-6 dB");
}

TEST_CASE("the pre-show check warns when sections would go unannounced", "[cuevoice]") {
    DemoSession d; auto& s = *d.session;
    const auto song = d.setlist().entries[0].songId;
    const app::CheckInputs inputs{true, "Test", 48000.0, 512, 2, {}};
    REQUIRE(s.commands().run("section.add", params({{"songId", song}, {"name", "Intro"}, {"startBar", 1}})).ok);
    CHECK(findItem(app::runPreShowCheck(s, inputs), "cues") == nullptr);   // one section: nothing is announced
    REQUIRE(s.commands().run("section.add", params({{"songId", song}, {"name", "Chorus"}, {"startBar", 5}})).ok);
    auto items = app::runPreShowCheck(s, inputs);
    const auto* cues = findItem(items, "cues");
    REQUIRE(cues != nullptr);
    CHECK(cues->level == app::CheckItem::Level::Warning);
    CHECK(cues->title == "The cue voice hasn't been made");
    CHECK(cues->detail == "Sections won't be announced. Make it in Setup, under Cue voice.");
    CHECK(app::mayEnterShow(items));
    // After midi and before files, as PreShowCheck.h lists them.
    const auto at = [&](const char* id) {
        return std::find_if(items.begin(), items.end(), [&](auto& i) { return i.id == id; }) - items.begin();
    };
    CHECK(at("cues") < at("files"));  CHECK(at("routing") < at("cues"));

    s.setVoiceRunner(countingRunner(std::make_shared<std::atomic<int>>(0)));
    REQUIRE(s.makeVoice(false, {}, {}).wasOk());
    waitUntilMade(s);
    CHECK(findItem(app::runPreShowCheck(s, inputs), "cues") == nullptr);   // made: every cue the setlist needs is there
    s.folder().root().getChildFile("cues/built-in/Chorus.wav").deleteFile();
    CHECK(findItem(app::runPreShowCheck(s, inputs), "cues") != nullptr);
    s.commands().run("sectionType.set", params({{"typeId", "chorus"}, {"cueClip", ""}}));
    CHECK(findItem(app::runPreShowCheck(s, inputs), "cues") == nullptr);   // a type with no cue needs none
}

TEST_CASE("the click's sound offers the band's own sounds and imports one", "[cuevoice]") {
    const auto choice = makeClickSoundChoice("wood", {"clicks/Solo Jeff.wav", "clicks/Shaker.aiff", "clicks/CON.wav"});
    CHECK(choice.items == juce::StringArray{"Wood block", "Beep", "Rim", "Solo Jeff", juce::String::fromUTF8("Import a sound…")});
    CHECK(choice.sounds == juce::StringArray{"wood", "beep", "rim", "clicks/Solo Jeff.wav", ""});
    CHECK(choice.selected == 0);
    const auto gone = makeClickSoundChoice("clicks/Gone.wav", {});   // the song's own, not on this computer yet
    CHECK(gone.items[3] == "Gone");  CHECK(gone.selected == 3);
    CHECK(makeClickSoundChoice("rim", {}).selected == 2);

    DemoSession d; auto& s = *d.session;
    const auto songId = d.setlist().entries[0].songId;
    sqlive::test::ScopedLook look;
    ui::SongEditorPage page(s);  page.setVisible(true);  page.openSong(songId);
    auto source = d.dir.dir().getChildFile("Solo Jeff.wav");  writeSine(source, 48000.0, 1, 0.1, 1000.0, 0.5f);
    page.inspector().importClickSound(source);                    // what "Import a sound…" does with the file chosen
    CHECK(s.model().song(songId)->click.sound == "clicks/Solo Jeff.wav");
    pumpFor(50);
    const auto& shown = page.inspector().clickSoundChoice();
    REQUIRE(juce::isPositiveAndBelow(shown.selected(), shown.items().size()));
    CHECK(shown.items()[shown.selected()] == "Solo Jeff");
    CHECK(shown.items()[shown.items().size() - 1] == juce::String::fromUTF8("Import a sound…"));
}

TEST_CASE("the cue voice page lays out in Setup at every window size", "[cuevoice]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::SetupPage setup(s, nullptr);
    setup.setSize(1100, 618);
    setup.mouseDown(eventOn(setup, setup.subTabBounds(SetupPage::SubTab::CueVoice).getCentre().toFloat()));
    REQUIRE(setup.subTab() == SetupPage::SubTab::CueVoice);
    CHECK(setup.cueVoice().isVisible());  CHECK_FALSE(setup.midi().isVisible());
    for (const auto& [w, h] : {std::pair{1100, 618}, std::pair{1280, 698}, std::pair{1920, 1018}}) {
        INFO(w << " x " << h);
        auto image = snapshot(setup, w, h);
        CHECK(childrenInside(setup));
        CHECK(fractionNotBackground(image) > 0.01);
        auto& page = setup.cueVoice();
        CHECK(page.topBounds().getRight() <= page.getWidth());
        CHECK(page.tableBounds().getBottom() <= page.getHeight());
        REQUIRE(page.cueChoiceView(3) != nullptr);
        CHECK(page.cueChoiceView(3)->getWidth() >= 120);
    }
    // The missing clips show in caution.
    CHECK(hasColour(snapshot(setup, 1280, 698), theme::caution));
}
