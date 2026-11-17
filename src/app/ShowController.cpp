#include "app/ShowController.h"

#include <algorithm>
#include <vector>

#include "app/ShowArrangement.h"
#include "app/WallClock.h"

namespace sqlive::app {

namespace {

int codeOf(ShowKey key) {
    switch (key) {
        case ShowKey::Space: return juce::KeyPress::spaceKey;
        case ShowKey::Escape: return juce::KeyPress::escapeKey;
        case ShowKey::Left: return juce::KeyPress::leftKey;
        case ShowKey::Right: return juce::KeyPress::rightKey;
        case ShowKey::Tab: return juce::KeyPress::tabKey;
        case ShowKey::Other: break;
    }
    return 0;                                      // no key: never tracked
}
}  // namespace

bool ShowController::DoublePress::press(double now) {
    if (armed && now - at < doubleMinSeconds) return false;   // a bounce or a duplicate: the first press still counts
    if (armed && now - at <= doubleEscSeconds) { armed = false;  return true; }
    armed = true;
    at = now;
    return false;
}

ShowController::ShowController(Session& s) : session_(s) {
    onAction = [this](Action a) { session_.perform(a); };
}

ShowController::~ShowController() { stopTimer(); }

void ShowController::act(Action a) {
    // A song started here (Start, or a section key while stopped): a tap from before it never counts toward ending it.
    if (a == Action::Start || (ShowArrangement::startStep(a) >= 0 && !session_.now().playing)) {
        tapped_stop_.reset();
        tapped_next_.reset();
    }
    if (onAction) onAction(a);
}

void ShowController::press(Source source, Action a, double now) {
    last_time_ = now;
    if (!held_.insert({source, a}).second) return;     // already held from here: one press, one action
    const bool playing = session_.now().playing;
    switch (a) {
        case Action::Start:
            if (!playing) act(Action::Start);      // this press is used: holding on never stops the song
            else stop_holds_[{source, a}] = now;
            break;
        case Action::Stop:
            if (playing) stop_holds_[{source, a}] = now;
            break;
        case Action::NextSongLater:                // sent on the release, unless held into the next song
        case Action::GoToNextSongNow:              // only ever held into it
            if (session_.now().inStep) next_holds_[{source, a}] = now;   // only while a step plays: never a count-in
            break;
        default: act(a); break;
    }
}

void ShowController::release(Source source, Action a, double now) {
    last_time_ = now;
    const bool wasHeld = held_.erase({source, a}) > 0;
    const bool wentOn = next_done_.erase({source, a}) > 0;
    stop_holds_.erase({source, a});
    next_holds_.erase({source, a});
    if (a == Action::NextSongLater && wasHeld && !wentOn) act(Action::NextSongLater);
}

bool ShowController::doubleTap(DoublePress& presses, TappedIn& in, double now) {
    const auto song = session_.now();
    if (!song.playing) {
        presses.reset();                           // only taps during a song count
        return false;
    }
    // Only taps in the same song: one started since the first tap, wherever from, forgets it.
    // Told by the song's serial, which only goes up: song time goes back at every loop, jump or repeated step.
    const bool sameSong = song.playingSerial == in.serial && song.playingIndex == in.index && song.playingSongId == in.songId;
    if (presses.armed && !sameSong) presses.reset();
    if (presses.press(now)) return true;
    if (presses.armed && presses.at == now) in = {song.playingIndex, song.playingSongId, song.playingSerial};
    return false;
}

// A button whose release can't be trusted: nothing is held, so a tap never stops a song by being "held".
void ShowController::tap(Action a, double now) {
    last_time_ = now;
    switch (a) {
        case Action::Start:
            if (!session_.now().playing) act(Action::Start);
            break;
        case Action::Stop:
            if (doubleTap(tapped_stop_, tapped_in_, now)) act(Action::Stop);
            break;
        case Action::GoToNextSongNow:
            if (doubleTap(tapped_next_, tapped_next_in_, now)) act(Action::GoToNextSongNow);
            break;
        default: act(a); break;
    }
}

void ShowController::buttonDown(Action a, double now, PedalKind kind) {
    if (kind == PedalKind::Tap) tap(a, now);
    else press(Source::Buttons, a, now);
    updateTimer();
}

void ShowController::buttonUp(Action a, double now) {
    release(Source::Buttons, a, now);
    updateTimer();
}

void ShowController::keyDown(int keyCode, int modifiers, double now) {
    last_time_ = now;
    const int code = sameKeyCode(keyCode);
    if (code == juce::KeyPress::escapeKey) {
        if (keys_.emplace(code, std::nullopt).second && esc_.press(now)) act(Action::Stop);   // a repeat is not a press
        return;
    }
    const auto action = controls_.of(session_.model().band()).actionForKey(code, modifiers);
    if (!action) return;                           // never tracked: no key-up is polled for it
    if (!keys_.emplace(code, action).second) return;   // a repeat of a key already held
    press(Source::Keys, *action, now);
}

void ShowController::keyUp(int keyCode, double now) {
    last_time_ = now;
    const auto found = keys_.find(sameKeyCode(keyCode));
    if (found == keys_.end()) return;
    const auto action = found->second;
    keys_.erase(found);
    if (action) release(Source::Keys, *action, now);
}

void ShowController::keyDown(ShowKey key, double now) { keyDown(codeOf(key), 0, now); }
void ShowController::keyUp(ShowKey key, double now) { keyUp(codeOf(key), now); }

void ShowController::tick(double now) {
    last_time_ = now;
    for (auto& [holder, since] : stop_holds_)
        if (now - since >= holdSeconds) {
            stop_holds_.clear();                   // used: once only, however many were held
            act(Action::Stop);
            return;
        }
    // Only while a step plays and nothing is stopping: otherwise the hold is dropped, and letting go of → is a tap.
    if (!next_holds_.empty() && !session_.now().inStep) next_holds_.clear();
    for (auto& [holder, since] : next_holds_)
        if (now - since >= holdSeconds) {
            for (auto& [used, at] : next_holds_) next_done_.insert(used);   // once only; their releases send nothing
            next_holds_.clear();
            act(Action::GoToNextSongNow);
            return;
        }
}

void ShowController::keyboardLost() {
    for (auto& [code, action] : keys_)
        if (action) {
            held_.erase({Source::Keys, *action});
            stop_holds_.erase({Source::Keys, *action});
            next_holds_.erase({Source::Keys, *action});
            next_done_.erase({Source::Keys, *action});
        }
    keys_.clear();                                 // a held button stays held: it doesn't need the keyboard
}

double ShowController::holdProgress() const {
    double progress = 0.0;
    for (auto& [holder, since] : stop_holds_) progress = std::max(progress, std::clamp((last_time_ - since) / holdSeconds, 0.0, 1.0));
    return progress;
}

double ShowController::nextSongHoldProgress() const {
    double progress = 0.0;
    for (auto& [holder, since] : next_holds_) progress = std::max(progress, std::clamp((last_time_ - since) / holdSeconds, 0.0, 1.0));
    return progress;
}

bool ShowController::keyPressed(const juce::KeyPress& k, juce::Component*) {
    keyDown(k.getKeyCode(), k.getModifiers().getRawFlags(), wallClockSeconds());
    updateTimer();
    return true;
}

bool ShowController::keyStateChanged(bool, juce::Component*) {
    releaseVanishedKeys(wallClockSeconds());
    updateTimer();
    return true;
}

// A key the system no longer reports as down was let go (or the keyboard was taken away): treat it as released. Only keys:
// a button held down has no key to look at, and is let go by its own buttonUp.
void ShowController::releaseVanishedKeys(double now) {
    std::vector<int> held;
    for (auto& entry : keys_) held.push_back(entry.first);
    for (int code : held)
        if (!juce::KeyPress::isKeyCurrentlyDown(code)) keyUp(code, now);
}

void ShowController::timerCallback() {
    const auto now = wallClockSeconds();
    releaseVanishedKeys(now);
    tick(now);
    updateTimer();
}

void ShowController::updateTimer() {
    if (keys_.empty() && held_.empty()) { if (isTimerRunning()) stopTimer(); }
    else if (!isTimerRunning()) startTimerHz(30);
}

}  // namespace sqlive::app
