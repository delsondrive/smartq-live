#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "app/LevelChecker.h"
#include "app/PreShowCheck.h"
#include "model/Effects.h"
#include "model/LevelMeasure.h"
#include "tests/support/DemoSession.h"
#include "tests/support/FileHold.h"
#include "tests/support/Params.h"

using namespace sqlive::app;
using sqlive::test::DemoSession;
using sqlive::test::params;

namespace { CheckInputs good() { return {true, "Test device", 48000.0, 512, 2, {true, false, 100}}; }
            const CheckItem* find(const std::vector<CheckItem>& items, const char* id) { for (auto& i : items) if (i.id == id) return &i; return nullptr; } }
TEST_CASE("a healthy show passes with plain lines", "[preshow]") {
    DemoSession d;
    auto items = runPreShowCheck(*d.session, good());
    CHECK(mayEnterShow(items));
    CHECK(find(items, "audio")->detail == "Test device, 48 kHz, 512 samples");
    CHECK(find(items, "routing")->detail == "click and cues only reach in-ear outputs");
    CHECK(find(items, "files")->level == CheckItem::Level::Ok);
    CHECK(find(items, "setlist")->detail == "3 songs");
    CHECK(find(items, "conflicts") == nullptr);  CHECK(find(items, "power") == nullptr);
}
TEST_CASE("what would go wrong on stage is caught", "[preshow]") {
    DemoSession d; auto& s = *d.session;
    auto inputs = good(); inputs.deviceOpen = false; inputs.power = {true, true, 64};
    auto& song = *s.model().song(d.setlist().entries[0].songId);
    s.folder().audioFile(song, song.parts[0]).deleteFile();
    auto problems = partFileProblems(s, song.id);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].partId == song.parts[0].id);  CHECK_FALSE(problems[0].inCloud);       // missing, not in the cloud
    auto items = runPreShowCheck(s, inputs);
    CHECK_FALSE(mayEnterShow(items));
    CHECK(find(items, "audio")->level == CheckItem::Level::Blocker);
    CHECK(std::count_if(items.begin(), items.end(), [](auto& i) { return i.id == "files"; }) == 1);
    CHECK(find(items, "files")->level == CheckItem::Level::Warning);
    CHECK(find(items, "files")->title == "1 part has no audio file");
    CHECK(find(items, "power")->detail == "64% left. Plug the laptop in.");
    auto entries = d.setlist().entries;                                           // a copy: removing changes the setlist
    for (auto& e : entries) s.commands().run("setlist.removeEntry", params({{"setlistId", s.setlistId()}, {"entryId", e.id}}));
    CHECK(find(runPreShowCheck(s, good()), "setlist")->level == CheckItem::Level::Blocker);
}
TEST_CASE("a different sample rate is a warning and an overlapping in-ear output a blocker", "[preshow]") {
    DemoSession d; auto& s = *d.session;
    auto inputs = good(); inputs.deviceRate = 44100.0;
    const auto rateItems = runPreShowCheck(s, inputs);
    auto audio = find(rateItems, "audio");
    CHECK(audio->level == CheckItem::Level::Warning);
    CHECK(audio->detail == "Test device, 44.1 kHz, 512 samples. Songs are converted to 48 kHz as they load.");
    auto pa = s.commands().run("output.add", params({{"rigId", s.rigId()}, {"name", "PA"}, {"firstChannel", 2}, {"stereo", true}, {"label", "foh"}}));
    CHECK_FALSE(pa.ok);                                           // a PA output may never share a channel: the command refuses it
    s.commands().run("output.add", params({{"rigId", s.rigId()}, {"name", "Drummer"}, {"firstChannel", 2}, {"stereo", true}, {"label", "monitor"}}));
    auto items = runPreShowCheck(s, good());                      // two in-ear outputs may overlap; checkRig blocks the show
    CHECK_FALSE(mayEnterShow(items));
    CHECK(std::any_of(items.begin(), items.end(), [](auto& i) { return i.id == "routing" && i.level == CheckItem::Level::Blocker; }));
}
TEST_CASE("a conflict copy is named, with where to choose which to keep", "[preshow]") {
    DemoSession d; auto& s = *d.session;
    auto song = s.folder().fileFor({sqlive::model::DocKind::Song, d.setlist().entries[0].songId});
    song.getSiblingFile("song-LAPTOP.json").replaceWithText(song.loadFileAsString());
    s.advanceWithoutDevice(0);                                    // the session's look at the band folder
    const auto items = runPreShowCheck(s, good());
    const auto* item = find(items, "conflicts");
    REQUIRE(item != nullptr);
    CHECK(item->title == "1 file has a conflict copy");
    CHECK(item->detail == "Two computers changed the same file. Click Check files to choose which to keep.");
}
TEST_CASE("changes that couldn't be saved yet are named before the show", "[preshow]") {
    DemoSession d; auto& s = *d.session;
    CHECK(find(runPreShowCheck(s, good()), "saving") == nullptr);
    auto band = s.folder().root().getChildFile("band.json");
    sqlive::test::ExclusiveFileHold hold(band);                   // the write is refused while this is held
    if (!hold.held()) SKIP("this system lets a file that is open be replaced");
    s.commands().run("band.rename", params({{"name", "Unsaved"}}));
    s.flushNow();
    REQUIRE(s.saveState() == SaveState::Failed);
    const auto items = runPreShowCheck(s, good());
    const auto* item = find(items, "saving");
    REQUIRE(item != nullptr);
    CHECK(item->level == CheckItem::Level::Warning);
    CHECK(item->title == "Some changes haven't been saved yet.");
    CHECK(item->detail == s.saveError());
    CHECK(mayEnterShow(items));
}

// ---- The cue and click sounds the setlist needs (the final review, I6) ----

TEST_CASE("the cue and click sounds the setlist needs are checked: missing ones named, ones in the cloud blocking", "[preshow]") {
    DemoSession d; auto& s = *d.session;
    const auto first = d.setlist().entries[0].songId, second = d.setlist().entries[1].songId;
    REQUIRE(s.commands().run("cue.add", params({{"songId", first}, {"bar", 3}, {"beat", 1.0}, {"clip", "cues/solo-now.wav"}})).ok);
    REQUIRE(s.commands().run("song.setClick", params({{"songId", second}, {"sound", "clicks/Shaker.wav"}})).ok);
    CHECK(clipsNeeded(*s.model().song(first), s.model().band(), {}) == juce::StringArray{"cues/solo-now.wav"});
    auto items = runPreShowCheck(s, good());
    REQUIRE(find(items, "cues") != nullptr);
    CHECK(find(items, "cues")->level == CheckItem::Level::Warning);
    CHECK(find(items, "cues")->title == "2 cue or click sounds are missing");
    CHECK(find(items, "cues")->detail == "solo-now.wav, Shaker.wav. They won't be heard on stage.");
    CHECK(mayEnterShow(items));

    const auto root = s.folder().root();
    root.getChildFile("cues").createDirectory();  root.getChildFile("clicks").createDirectory();
    root.getChildFile("cues/solo-now.wav").replaceWithText("a take");   // the guitarist's recording has synced
    root.getChildFile("clicks/Shaker.wav").replaceWithText("a click");
    items = runPreShowCheck(s, good());
    CHECK(find(items, "cues") == nullptr);
    CHECK(find(items, "files")->level == CheckItem::Level::Ok);

    // On this laptop it is still only in the cloud: the existing "still in the cloud" blocker, with Download now.
    if (!sqlive::test::markAsInTheCloud(root.getChildFile("cues/solo-now.wav"))) SKIP("no cloud placeholders here");
    items = runPreShowCheck(s, good());
    CHECK_FALSE(mayEnterShow(items));
    const auto* files = find(items, "files");
    REQUIRE(files != nullptr);
    CHECK(files->level == CheckItem::Level::Blocker);
    CHECK(files->title == "1 audio file is still in the cloud");
    CHECK(files->detail == "OneDrive hasn't downloaded it to this laptop (cue and click sounds: solo-now.wav).");
    CHECK(files->fixLabel == "Download now");
    const auto problems = clipFileProblems(s, first, {});
    REQUIRE(problems.size() == 1);  CHECK(problems[0].inCloud);  CHECK(problems[0].path == "cues/solo-now.wav");
}
TEST_CASE("the section cues an entry's arrangement announces are checked; a missing voice says so", "[preshow]") {
    DemoSession d(true); auto& s = *d.session;                     // sections typed Intro, Verse, Chorus …: the built-in voice
    auto items = runPreShowCheck(s, good());
    REQUIRE(find(items, "cues") != nullptr);
    CHECK(find(items, "cues")->title == "The cue voice hasn't been made");
    CHECK(find(items, "cues")->detail.startsWith("Sections won't be announced (Intro.wav, Verse.wav, Chorus.wav"));
}

// ---- Effects: every setlist song prepared, and a Limit working hard (the effects plan, Task 9) ----

namespace {
sqlive::model::EffectDoc gainEq(double db) {
    auto e = sqlive::model::defaultEffect("eq");
    e.values.set("output_gain", db);
    return e;
}
}  // namespace

TEST_CASE("Show mode can't start until every setlist song is prepared", "[preshow]") {
    DemoSession d; auto& s = *d.session;
    s.preparer().setPaused(true);                                 // nothing is prepared in the background meanwhile
    CHECK(find(runPreShowCheck(s, good()), "effects") == nullptr);   // no song has effects
    for (int i = 0; i < 3; ++i) {
        const auto& song = *s.model().song(d.setlist().entries[(std::size_t) i].songId);
        const EffectsTarget keys{song.id, song.parts[0].id, {}, {}};
        REQUIRE(s.addEffect(keys, "eq"));
        s.setEffect(keys, 0, gainEq(3.0 + i), "gain");
    }
    auto items = runPreShowCheck(s, good());
    const auto* item = find(items, "effects");
    REQUIRE(item != nullptr);
    CHECK(item->level == CheckItem::Level::Blocker);
    CHECK(item->title == "3 songs need their effects prepared.");
    CHECK(item->fixLabel == "Prepare now");
    CHECK_FALSE(mayEnterShow(items));

    s.preparer().setPaused(false);
    s.prepareForShow();
    CHECK(s.preparingForShow());
    const auto* preparing = find(runPreShowCheck(s, good()), "effects");
    REQUIRE(preparing != nullptr);                                // still a blocker while it works, with how far it has got
    CHECK(preparing->level == CheckItem::Level::Blocker);
    CHECK(preparing->title.startsWith(juce::String::fromUTF8("Preparing effects\xe2\x80\xa6 ")));
    CHECK(preparing->fixLabel.isEmpty());
    for (int i = 0; i < 400 && s.preparingForShow(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    REQUIRE_FALSE(s.preparingForShow());
    items = runPreShowCheck(s, good());
    CHECK(find(items, "effects") == nullptr);
    CHECK(mayEnterShow(items));
}
TEST_CASE("the pre-show check names a Limit working hard", "[preshow]") {
    sqlive::model::OutputPeak peak;
    peak.outputId = "foh";  peak.limitDb = 4.47f;  peak.limitBar = 97;
    CHECK(sqlive::model::describeLimit(peak, "FOH", "Hold The Line") == "FOH's Limit takes 4.5 dB in Hold The Line at bar 97.");

    DemoSession d; auto& s = *d.session;
    const auto& rig = *s.model().rig(s.rigId());
    const EffectsTarget out{{}, {}, rig.id, rig.outputs[0].id};
    const auto outName = rig.outputs[0].name;
    s.levelChecker().checkSetlist();  REQUIRE(s.levelChecker().waitUntilIdle(30000));
    const auto limits = [&] {
        std::vector<CheckItem> found;
        for (auto& i : runPreShowCheck(s, good()))
            if (i.id == "levels" && i.title.contains("'s Limit takes")) found.push_back(i);
        return found;
    };
    CHECK(limits().empty());                                      // no Limit
    REQUIRE(s.addEffect(out, "limit"));                           // as it starts: a ceiling of 0 dB, nothing pushed into it
    auto limit = s.effects(out)[0];
    s.levelChecker().checkSetlist();  REQUIRE(s.levelChecker().waitUntilIdle(30000));
    CHECK(limits().empty());
    limit.values.set("input_gain", 18.0);                         // pushed 18 dB into it
    s.setEffect(out, 0, limit, "in");
    s.levelChecker().checkSetlist();  REQUIRE(s.levelChecker().waitUntilIdle(30000));
    const auto found = limits();
    REQUIRE(found.size() == 3);                                   // one per song, on that output
    const auto first = s.model().song(d.setlist().entries[0].songId)->title;
    CHECK(found[0].level == CheckItem::Level::Warning);
    CHECK(found[0].title.startsWith(outName + "'s Limit takes "));
    CHECK(found[0].title.contains(" dB in " + first + " at bar "));
    CHECK(mayEnterShow(runPreShowCheck(s, good())));              // a warning: the band's call
}

namespace {
// Each of the setlist's first three songs with an EQ on its first part, and Prepare now run to its end.
void prepareThree(DemoSession& d) {
    auto& s = *d.session;
    for (int i = 0; i < 3; ++i) {
        const auto& song = *s.model().song(d.setlist().entries[(std::size_t) i].songId);
        const EffectsTarget keys{song.id, song.parts[0].id, {}, {}};
        REQUIRE(s.addEffect(keys, "eq"));
        s.setEffect(keys, 0, gainEq(3.0 + i), "gain");
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(2500);   // past the 2 s wait: the background has its turn first
    s.prepareForShow();
    for (int i = 0; i < 600 && s.preparingForShow(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    REQUIRE_FALSE(s.preparingForShow());
    REQUIRE(s.preparer().waitUntilIdle(30000));
    REQUIRE(s.levelChecker().waitUntilIdle(30000));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
}
}  // namespace

TEST_CASE("a prepared-audio store that can't be written warns, and the show can still start", "[preshow]") {
    DemoSession d; auto& s = *d.session;
    const auto store = d.dir.dir().getChildFile("Prepared audio");   // next to the settings file
    REQUIRE(store.deleteRecursively());
    REQUIRE(store.replaceWithText("a file where the folder should be"));   // as a full disk or a folder that can't be written
    prepareThree(d);
    auto items = runPreShowCheck(s, good());
    const auto* item = find(items, "effects");
    REQUIRE(item != nullptr);
    CHECK(item->level == CheckItem::Level::Warning);
    CHECK(item->title == "Prepared audio couldn't be saved on this computer");
    CHECK(item->detail == "3 songs will be prepared as they load, on stage. Is the disk full?");
    CHECK(item->fixLabel == "Try again");
    CHECK(mayEnterShow(items));

    REQUIRE(store.deleteFile());                                     // room again: Try again prepares them
    s.prepareForShow();
    for (int i = 0; i < 600 && s.preparingForShow(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    REQUIRE_FALSE(s.preparingForShow());
    items = runPreShowCheck(s, good());
    CHECK(find(items, "effects") == nullptr);
    CHECK(mayEnterShow(items));
}

TEST_CASE("a setlist whose prepared audio is over the limit keeps it all, and says so", "[preshow]") {
    DemoSession d; auto& s = *d.session;
    s.preparedStore().setLimit(1);                                   // smaller than any one song's
    prepareThree(d);
    for (int i = 0; i < 3; ++i)                                      // none trimmed away by another's
        CHECK(s.effectsState(d.setlist().entries[(std::size_t) i].songId) == EffectsPreparer::State::Prepared);
    auto items = runPreShowCheck(s, good());
    const auto* item = find(items, "effects");
    REQUIRE(item != nullptr);
    CHECK(item->level == CheckItem::Level::Warning);
    CHECK(item->title.startsWith("The setlist's prepared audio takes "));
    CHECK(item->title.endsWith(", more than the limit of less than 1 MB"));
    CHECK(item->detail == "It is all kept on this computer all the same; other songs' prepared audio goes first.");
    CHECK(item->fixLabel.isEmpty());
    CHECK(mayEnterShow(items));
}
