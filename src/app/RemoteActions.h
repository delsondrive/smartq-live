#pragma once

#include <juce_core/juce_core.h>

#include "app/Session.h"
#include "app/Settings.h"

namespace sqlive::app {

// What a phone on the remote may ask of the show. Message thread only, one request at a time, as the keyboard's actions:
// each is Session::perform with the same Action value.

// The actions a phone may ask for by name, as model::actionInfos() names them: start, stop, nextSongLater, nextSongEarlier,
// nextSectionLater, nextSectionEarlier, section1 … section10, loopSection, cancelSection, goToNextSongNow. Not toggleView,
// which is the laptop's own business.
juce::StringArray remoteActionNames();
// What the phone's page sends: every name above, then the two holds, stopHold and nextNowHold. The page's controls and the
// Setup page's PHONE column are both checked against this one list.
juce::StringArray remotePageActions();
// The hold an action needs first: "stopHold" for "stop", "nextNowHold" for "goToNextSongNow"; "" for any other.
juce::String remoteHoldFor(const juce::String& action);

// A hold counts when the action comes this long after it, from the same phone, in the same song.
inline constexpr double remoteHoldMinSeconds = 0.9;
inline constexpr double remoteHoldMaxSeconds = 3.0;

struct RemoteActionResult {
    int status = 400;                     // 200 done; 403 not allowed (message says why, for the phone to show); 400 unknown
    juce::String message;                 // "" when done
};

// Performs a phone's action. Refused (403), in this order: a phone that isn't let in, waits, or may only watch; then (400)
// a name that isn't one of remotePageActions(); then (403) outside Show mode.
//
// A phone never stops a song, nor goes to the next one, with one request. Stop and "go to the next song now" each need their
// hold first (remoteHoldFor), from the same phone, 0.9 to 3.0 s before (nowSeconds: any steady clock), in the same song
// (NowPlaying::playingSerial); otherwise 403 "Hold to stop." or "Hold to go to the next song now.". Each phone keeps only its
// last request's hold: any request of its own that gets this far uses it up, so a hold is good for one action at most.
// nextNowHold and goToNextSongNow are refused while the session can't go on (NowPlaying::canGoOn).
//
// A phone never starts a song by accident: section1 … section10 are refused unless a song plays (stopped, the session would
// start the next song from that step). start starts it, as the laptop's Start does.
RemoteActionResult performRemoteAction(Session&, const juce::String& action, PhoneAccess, const juce::String& phoneId,
                                       double nowSeconds);

}  // namespace sqlive::app
