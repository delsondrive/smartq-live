#pragma once

#include <memory>

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

    DemoSession() {
        const auto root = dir.dir().getChildFile("Demo band");
        model::createDemoBand(root);
        settings = std::make_unique<app::Settings>(dir.dir().getChildFile("settings.json"));
        session = std::make_unique<app::Session>(*settings, root);
        session->setDevice(48000.0, 2);
        settle();
    }

    // Waits for the cache, then runs pending message-thread work.
    void settle() {
        session->cache().waitUntilIdle(20000);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }

    const model::SetlistDoc& setlist() const { return *session->model().setlist(session->setlistId()); }
};

}  // namespace sqlive::test
