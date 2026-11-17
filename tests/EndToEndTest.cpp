#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include "engine/Engine.h"
#include "engine/OfflineRender.h"
#include "model/BandFolder.h"
#include "model/CoreCommands.h"
#include "model/DemoBand.h"
#include "model/DocumentsJson.h"
#include "model/Routing.h"
#include "model/SongAudio.h"
#include "model/SongMidi.h"
#include "model/Units.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/TempDir.h"

using namespace sqlive;
using namespace sqlive::model;
using sqlive::test::params;

TEST_CASE("the demo band plays", "[e2e]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Demo band");
    REQUIRE(createDemoBand(root).wasOk());
    BandFolder folder(root); BandModel model; auto report = folder.load(model);
    CHECK(report.notes.isEmpty());
    REQUIRE(model.songIds().size() == 3);
    REQUIRE(model.setlistIds().size() == 1);
    CHECK(model.setlist(model.setlistIds()[0])->entries.size() == 3);
    CommandLayer commands(model); registerCoreCommands(commands);
    auto& rig = *model.rig(model.rigIds()[0]);
    CHECK(checkRig(model.band(), rig, 2).empty());
    auto& song = *model.song(model.setlist(model.setlistIds()[0])->entries[0].songId);
    CHECK(song.countInBars == 1);
    std::map<juce::String, std::shared_ptr<const engine::PartAudio>> audio;
    for (auto& part : song.parts) audio[part.id] = loadPartAudio(folder.audioFile(song, part), 48000.0, [](double) { return true; });
    for (auto& [id, a] : audio) REQUIRE(a != nullptr);
    auto play = [&](const RigDoc& r) {
        engine::Engine e; e.prepare(48000.0, 512); e.setMix(buildMixSetup(model.band(), r, 2));
        auto prepared = std::make_shared<const engine::PreparedSong>(buildPreparedSong(song, model.band(), r, audio, 48000.0));
        e.cue(prepared); e.start();
        return engine::renderOffline(e, 2, -prepared->start + prepared->end + 1000, 512);
    };
    auto out = play(rig);
    float peak = 0; for (float x : out[0]) peak = std::max(peak, std::abs(x));
    CHECK(peak > 0.05f);  CHECK(peak < 1.0f);                       // heard, and not clipping
    CHECK(out[0][100] != 0.0f);                                     // the count-in's first click
    CHECK(out[0].back() == 0.0f);
    auto click = model.band().roles[0].id;
    commands.run("mix.setLevel", params({{"rigId", rig.id}, {"outputId", rig.outputs[0].id}, {"roleId", click}, {"db", juce::var()}}));
    auto quiet = play(*model.rig(rig.id));
    CHECK(quiet[0][100] == 0.0f);                                   // no click in the count-in now
    audio.erase(song.parts[0].id);                                  // one part's file is missing: the song still plays
    CHECK_NOTHROW(play(*model.rig(rig.id)));
}

TEST_CASE("the demo band's audio is the same every time, 24-bit at 48 kHz and no louder than -16 dB", "[e2e]") {
    sqlive::test::TempDir t;
    auto first = t.dir().getChildFile("One"), second = t.dir().getChildFile("Two");
    REQUIRE(createDemoBand(first).wasOk());
    REQUIRE(createDemoBand(second).wasOk());
    CHECK(createDemoBand(first).failed());                         // there is a band there already

    auto wavs = first.findChildFiles(juce::File::findFiles, true, "*.wav");
    CHECK(wavs.size() == 9);
    juce::WavAudioFormat format;
    for (auto& wav : wavs) {
        INFO(wav.getFullPathName().toStdString());
        CHECK(wav.hasIdenticalContentTo(second.getChildFile(wav.getRelativePathFrom(first))));
        std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(wav.createInputStream().release(), true));
        REQUIRE(reader != nullptr);
        CHECK(reader->sampleRate == 48000.0);
        CHECK(reader->bitsPerSample == 24);
        juce::AudioBuffer<float> buffer((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
        float peak = 0.0f;
        for (int c = 0; c < buffer.getNumChannels(); ++c) peak = std::max(peak, buffer.getMagnitude(c, 0, buffer.getNumSamples()));
        CHECK(peak > 0.01f);
        CHECK(peak <= dbToGain(-16.0f));
    }
}

TEST_CASE("no click reaches the PA through an in-ear output that shares its channel", "[e2e]") {
    sqlive::test::TempDir t; auto root = t.dir().getChildFile("Band");
    REQUIRE(BandFolder::create(root, "Band").wasOk());
    BandFolder folder(root); BandModel model; folder.load(model);
    CommandLayer commands(model); registerCoreCommands(commands);
    const auto rigId = model.rigIds()[0];
    // The command refuses this layout, so it is made the way a hand-edited file would have it.
    RigDoc rig = *model.rig(rigId);
    OutputDoc pa; pa.id = newId(); pa.name = "PA"; pa.firstChannel = 2; pa.stereo = false; pa.label = OutputLabel::Foh;
    rig.outputs.push_back(pa);                                     // the in-ear Headphones stay on 1/2, click and all
    model.putRig(rig);
    REQUIRE(model.rig(rigId)->outputs[0].mix.count(model.band().roles[0].id) == 1);

    engine::Engine e; e.prepare(48000.0, 512); e.setMix(buildMixSetup(model.band(), *model.rig(rigId), 2));
    engine::ClickSetup click; click.enabled = true;
    auto song = std::make_shared<const engine::PreparedSong>(
        engine::prepareSong("click", engine::TempoMap{}, 1, 0, {}, click, 48000.0, 96000));   // nothing but the click
    e.cue(song); e.start();
    auto out = engine::renderOffline(e, 2, 96000 * 2, 512);
    CHECK(std::all_of(out[1].begin(), out[1].end(), [](float x) { return x == 0.0f; }));   // channel 2 is the PA's
}

TEST_CASE("the demo band's MIDI is two pedalboards with changes in every song, and Undo can't take it away", "[e2e]") {
    sqlive::test::DemoSession plain;
    CHECK(plain.session->model().band().midiDevices.empty());      // createDemoBand alone stays as it was
    sqlive::test::DemoSession d(true);
    const auto& m = d.session->model();
    const auto& devices = m.band().midiDevices;
    REQUIRE(devices.size() == 2);
    CHECK(devices[0].name == "Guitar 1");  CHECK(devices[1].name == "Guitar 2");
    const auto patchNames = [](const MidiDeviceDoc& device) {
        juce::StringArray names;
        for (const auto& p : device.patches) names.add(juce::String(p.program) + " " + p.name);
        return names.joinIntoString(", ");
    };
    CHECK(patchNames(devices[0]) == "4 Clean, 12 Crunch, 20 Lead");
    CHECK(patchNames(devices[1]) == "2 Rhythm, 9 Ambient");
    const auto& rig = *m.rig(m.rigIds()[0]);
    CHECK(rig.midiChannels.at(devices[0].id) == 1);  CHECK(rig.midiChannels.at(devices[1].id) == 2);
    const auto lane = [&](const SongDoc& song, const MidiDeviceDoc& device) {
        juce::StringArray changes;
        for (const auto& marker : song.midiLanes.at(device.id))
            changes.add(markerLabel(marker, &device) + " at " + juce::String(marker.bar) + "." + juce::String((int) marker.beat));
        return changes.joinIntoString(", ");
    };
    const auto& first = *m.song(d.setlist().entries[0].songId);
    REQUIRE(first.title == "First Light");
    CHECK(lane(first, devices[0]) == "Clean at 1.1, Crunch at 9.1, Clean at 13.1");
    CHECK(lane(first, devices[1]) == "Rhythm at 1.1, Ambient at 13.1");
    for (const auto& entry : d.setlist().entries) {
        const auto& song = *m.song(entry.songId);
        INFO(song.title);
        size_t later = 0;
        for (const auto& device : devices) {
            const auto& changes = song.midiLanes.at(device.id);
            REQUIRE_FALSE(changes.empty());
            CHECK(changes.front().bar == 1);
            later += changes.size() - 1;
        }
        CHECK(later >= 2);  CHECK(later <= 3);
    }
    CHECK_FALSE(d.session->commands().canUndo());
}

TEST_CASE("the demo band's sections have types, First Light has a long ending and a fade, and Undo can't take them away",
          "[e2e]") {
    sqlive::test::DemoSession plain;
    for (const auto& entry : plain.setlist().entries)              // createDemoBand alone stays as it was
        CHECK(plain.session->model().song(entry.songId)->sections.empty());
    sqlive::test::DemoSession d(true);
    const auto& m = d.session->model();
    const auto typeName = [&](const juce::String& typeId) {
        for (const auto& type : m.band().sectionTypes)
            if (type.id == typeId) return type.name;
        return juce::String("?");
    };
    const auto sections = [&](const SongDoc& song) {
        juce::StringArray all;
        for (const auto& s : song.sections)
            all.add(s.name + " (" + typeName(s.typeId) + ") " + juce::String(s.startBar) + "-" + juce::String(s.endBar - 1));
        return all.joinIntoString(", ");
    };
    const auto& entries = d.setlist().entries;
    REQUIRE(entries.size() == 3);
    const auto& first = *m.song(entries[0].songId);
    REQUIRE(first.title == "First Light");
    CHECK(sections(first) == "Intro (Intro) 1-4, Verse 1 (Verse) 5-8, Chorus (Chorus) 9-12, Outro (Outro) 13-16");
    CHECK(sections(*m.song(entries[1].songId))
          == "Intro (Intro) 1-1, Verse 1 (Verse) 2-3, Pre-chorus (Pre-chorus) 4-4, Chorus (Chorus) 5-6, Verse 2 (Verse) 7-7, "
             "Chorus (Chorus) 8-9, Solo (Solo) 10-10, Outro (Outro) 11-12");
    CHECK(sections(*m.song(entries[2].songId))
          == "Intro (Intro) 1-4, Verse 1 (Verse) 5-8, Chorus (Chorus) 9-16, Bridge (Bridge) 17-20, Outro (Outro) 21-24");

    REQUIRE(first.arrangements.size() == 1);
    CHECK(first.arrangements[0].name == "Long ending");
    juce::StringArray steps;
    for (const auto& id : first.arrangements[0].steps)
        for (const auto& s : first.sections)
            if (s.id == id) steps.add(s.name);
    CHECK(steps.joinIntoString(", ") == "Intro, Verse 1, Chorus, Chorus, Outro");

    const auto perc = std::find_if(first.parts.begin(), first.parts.end(), [](const PartDoc& p) { return p.name == "Perc"; });
    REQUIRE(perc != first.parts.end());
    REQUIRE(first.automation.count(perc->id) == 1);
    const auto& fade = first.automation.at(perc->id);
    REQUIRE(fade.size() == 2);                                     // silent at the Intro's start, full by the Verse
    CHECK(fade[0].bar == 1);  CHECK(fade[0].db == kMinDb);
    CHECK(fade[1].bar == 5);  CHECK(fade[1].db == 0.0f);
    CHECK(first.automation.size() == 1);                           // nothing else is automated
    CHECK_FALSE(d.session->commands().canUndo());
}
