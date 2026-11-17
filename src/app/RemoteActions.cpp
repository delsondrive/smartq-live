#include "app/RemoteActions.h"

#include <cstdint>
#include <map>
#include <optional>

#include "app/Controls.h"
#include "app/ShowArrangement.h"

namespace sqlive::app {

namespace {

const juce::String stopHoldName = "stopHold", nextNowHoldName = "nextNowHold";

// A phone's last hold: which, when, and in which song. Message thread only, like the requests; one per phone.
struct Hold {
    const Session* session = nullptr;
    juce::String name;                                        // stopHold or nextNowHold
    double at = 0.0;
    std::uint64_t serial = 0;                                 // NowPlaying::playingSerial when it was made
};

std::map<juce::String, Hold>& holds() {
    static std::map<juce::String, Hold> byPhone;
    return byPhone;
}

// The phone's hold, taken out: whatever it asks now uses it up.
std::optional<Hold> takeHold(const juce::String& phoneId) {
    auto& all = holds();
    const auto found = all.find(phoneId);
    if (found == all.end()) return std::nullopt;
    auto hold = found->second;
    all.erase(found);
    return hold;
}

bool holdCounts(const std::optional<Hold>& hold, const juce::String& name, const Session& session, double nowSeconds) {
    if (!hold || hold->session != &session || hold->name != name) return false;
    if (hold->serial != session.now().playingSerial) return false;
    const double held = nowSeconds - hold->at;
    return held >= remoteHoldMinSeconds && held <= remoteHoldMaxSeconds;
}

RemoteActionResult done() { return {200, {}}; }
RemoteActionResult refused(const juce::String& why) { return {403, why}; }

}  // namespace

juce::StringArray remoteActionNames() {
    juce::StringArray names;
    for (const auto action : allActions())
        if (action != Action::ToggleView) names.add(actionName(action));
    return names;
}

juce::StringArray remotePageActions() {
    auto names = remoteActionNames();
    names.add(stopHoldName);
    names.add(nextNowHoldName);
    return names;
}

juce::String remoteHoldFor(const juce::String& action) {
    if (action == actionName(Action::Stop)) return stopHoldName;
    if (action == actionName(Action::GoToNextSongNow)) return nextNowHoldName;
    return {};
}

RemoteActionResult performRemoteAction(Session& session, const juce::String& action, PhoneAccess access,
                                       const juce::String& phoneId, double nowSeconds) {
    JUCE_ASSERT_MESSAGE_THREAD
    switch (access) {
    case PhoneAccess::FullControl: break;
    case PhoneAccess::ViewOnly: return refused("This phone can only watch.");
    case PhoneAccess::Waiting: return refused("This phone is waiting to be let in on the laptop.");
    case PhoneAccess::Refused: return refused("This phone isn't let in.");
    }
    if (!remotePageActions().contains(action)) return {400, "There's no such action."};
    if (session.mode() != Mode::Show) return refused("The laptop isn't in Show mode.");

    const auto hold = takeHold(phoneId);
    const auto now = session.now();
    if (action == stopHoldName || action == nextNowHoldName) {
        if (action == nextNowHoldName && !now.canGoOn) return refused("There's nothing to go on to yet.");
        holds()[phoneId] = {&session, action, nowSeconds, now.playingSerial};
        return done();
    }

    const auto chosen = actionFromName(action);
    jassert(chosen.has_value());                              // every name in remoteActionNames() is an action's
    if (!chosen) return {400, "There's no such action."};
    if (const auto needs = remoteHoldFor(action); needs.isNotEmpty() && !holdCounts(hold, needs, session, nowSeconds))
        return refused(*chosen == Action::Stop ? "Hold to stop." : "Hold to go to the next song now.");
    if (*chosen == Action::GoToNextSongNow && !now.canGoOn) return refused("There's nothing to go on to yet.");
    // Stopped, a section would start the next song (Session::perform): only Start does that from a phone.
    if (ShowArrangement::startStep(*chosen) >= 0 && !(now.playing && !now.inGap))
        return refused("Choose a section while a song plays.");
    session.perform(*chosen);
    return done();
}

}  // namespace sqlive::app
