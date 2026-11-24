#pragma once

#include <memory>

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "app/Settings.h"
#include "model/DemoBand.h"
#include "tests/support/TempDir.h"

namespace sqlive::test {

// A session on a fresh demo band, with its own settings file in the temp folder and a 48 kHz stereo device that is never
// opened. The GUI initialiser is the first member, so it is made first and destroyed last: the session's timer, its
// ChangeBroadcaster and the cache's AsyncUpdater need the message manager.
struct DemoSession {
    juce::ScopedJuceInitialiser_GUI gui;
    TempDir dir;
    std::unique_ptr<app::Settings> settings;
    std::unique_ptr<app::Session> session;

    // full: the demo band with its MIDI and its sections too (model::addDemoMidi, model::addDemoSections), as the app and
    // the screenshots make it.
    // cacheBudgetBytes: the song cache's (SongCache); 1 keeps only the two songs it always keeps.
    explicit DemoSession(bool full = false, std::int64_t cacheBudgetBytes = app::SongCache::defaultBudgetBytes()) {
        const auto root = dir.dir().getChildFile("Demo band");
        REQUIRE(model::createDemoBand(root).wasOk());               // else no setlist: a later test would crash on it
        settings = std::make_unique<app::Settings>(dir.dir().getChildFile("settings.json"));
        session = std::make_unique<app::Session>(*settings, root, cacheBudgetBytes);
        session->setDevice(48000.0, 2);
        if (full) {
            REQUIRE(model::addDemoMidi(session->commands()).wasOk());
            REQUIRE(model::addDemoSections(session->commands()).wasOk());
            session->commands().clearHistory();
        }
        settle();
    }

    // Waits for the cache, then for the cue clips the songs it loaded asked for, then runs pending message-thread work.
    void settle() {
        session->cache().waitUntilIdle(20000);
        session->cues().waitUntilIdle(20000);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }

    const model::SetlistDoc& setlist() const { return *session->model().setlist(session->setlistId()); }

    // For view tests that change the documents while a show runs, to see the views follow (a song renamed, an entry gone):
    // lets every command through in Show mode too, which the app itself never does (Session's guard; SessionTest tests it).
    void allowEditsInShow() { session->commands().setGuard({}); }
};

}  // namespace sqlive::test
