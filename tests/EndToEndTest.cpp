#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include "engine/Engine.h"
#include "engine/OfflineRender.h"
#include "model/BandFolder.h"
#include "model/CoreCommands.h"
#include "model/DemoBand.h"
#include "model/Routing.h"
#include "model/SongAudio.h"
#include "model/Units.h"
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
    std::vector<std::shared_ptr<const engine::PartAudio>> audio;
    for (auto& part : song.parts) audio.push_back(loadPartAudio(folder.audioFile(song, part), 48000.0, [](double) { return true; }));
    for (auto& a : audio) REQUIRE(a != nullptr);
    auto play = [&](const RigDoc& r) {
        engine::Engine e; e.prepare(48000.0, 512, 2); e.setMix(buildMixSetup(model.band(), r, 2));
        auto prepared = std::make_shared<const engine::PreparedSong>(buildPreparedSong(song, model.band(), audio, 48000.0));
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
    audio[0] = nullptr;                                             // one part's file is missing: the song still plays
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
