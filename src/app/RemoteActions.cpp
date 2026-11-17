#include "app/RemoteActions.h"

#include "app/Controls.h"
#include "app/ShowArrangement.h"

namespace sqlive::app {

namespace {

const juce::String stopHoldName = "stopHold", nextNowHoldName = "nextNowHold";

// Clock readings are doubles: a hold exactly at an edge of the window still counts.
constexpr double holdEdgeSlack = 1e-6;

bool holdCounts(const std::optional<RemoteHolds::Hold>& hold, const juce::String& name, const Session& session,
                double nowSeconds) {
    if (!hold || hold->name != name) return false;
    if (hold->serial != session.now().playingSerial) return false;
    const double held = nowSeconds - hold->at;
    return held >= remoteHoldMinSeconds - holdEdgeSlack && held <= remoteHoldMaxSeconds + holdEdgeSlack;
}

RemoteActionResult done() { return {200, {}}; }
RemoteActionResult refused(const juce::String& why) { return {403, why}; }

// Why a phone that may not act is refused. Anything but the known values reads as not let in.
RemoteActionResult refusedFor(PhoneAccess access) {
    switch (access) {
    case PhoneAccess::ViewOnly: return refused("This phone can only watch.");
    case PhoneAccess::Waiting: return refused("This phone is waiting to be let in on the laptop.");
    case PhoneAccess::Refused:
    case PhoneAccess::FullControl: break;
    }
    return refused("This phone isn't let in.");
}

}  // namespace

std::optional<RemoteHolds::Hold> RemoteHolds::take(const juce::String& phoneId) {
    const auto found = byPhone_.find(phoneId);
    if (found == byPhone_.end()) return std::nullopt;
    auto hold = found->second;
    byPhone_.erase(found);
    return hold;
}

void RemoteHolds::put(const juce::String& phoneId, Hold hold) { byPhone_[phoneId] = std::move(hold); }

juce::StringArray remoteActionNames() {
    static constexpr Action allowed[] = {
        Action::Start, Action::Stop, Action::NextSongLater, Action::NextSongEarlier, Action::NextSectionLater,
        Action::NextSectionEarlier, Action::Section1, Action::Section2, Action::Section3, Action::Section4,
        Action::Section5, Action::Section6, Action::Section7, Action::Section8, Action::Section9, Action::Section10,
        Action::LoopSection, Action::CancelSection, Action::GoToNextSongNow};
    juce::StringArray names;
    for (const auto action : allowed) names.add(actionName(action));
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

std::optional<RemoteActionResult> remoteRefusal(PhoneAccess access) {
    if (access == PhoneAccess::FullControl) return std::nullopt;
    return refusedFor(access);
}

RemoteActionResult performRemoteAction(Session& session, RemoteHolds& holds, const juce::String& action,
                                       PhoneAccess access, const juce::String& phoneId, double nowSeconds) {
    JUCE_ASSERT_MESSAGE_THREAD
    if (access != PhoneAccess::FullControl) return refusedFor(access);
    if (!remotePageActions().contains(action)) return {400, "There's no such action."};
    if (session.mode() != Mode::Show) return refused("The laptop isn't in Show mode.");

    const auto hold = holds.take(phoneId);
    const auto now = session.now();
    if (action == stopHoldName || action == nextNowHoldName) {
        if (action == nextNowHoldName && !now.canGoOn) return refused("There's nothing to go on to yet.");
        holds.put(phoneId, {action, nowSeconds, now.playingSerial});
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
