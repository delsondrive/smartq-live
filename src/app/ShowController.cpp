#include "app/ShowController.h"

#include <algorithm>
#include <vector>

namespace sqlive::app {

namespace {
double clockSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

// A letter's key code in one case, so a key pressed with and without Caps Lock is the same key.
int sameCase(int keyCode) { return keyCode >= 'a' && keyCode <= 'z' ? keyCode - 'a' + 'A' : keyCode; }

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

ShowController::ShowController(Session& s) : session_(s) {
    onAction = [this](Action a) { session_.perform(a); };
}

ShowController::~ShowController() { stopTimer(); }

void ShowController::act(Action a) {
    if (onAction) onAction(a);
}

const ControlMap& ShowController::controls() {
    const auto& now = session_.model().band().controls;
    if (now != controls_seen_) {
        controls_seen_ = now;
        controls_ = ControlMap(now);
    }
    return controls_;
}

void ShowController::press(Action a, double now) {
    last_time_ = now;
    if (!held_.insert(a).second) return;           // already held: one press, one action
    if (a == Action::Start) {
        if (!session_.now().playing) act(Action::Start);       // this press is used: holding on never stops the song
        else stop_hold_since_ = now;
        return;
    }
    act(a);                                        // Stop stops at once; the others act once
}

void ShowController::release(Action a, double now) {
    last_time_ = now;
    held_.erase(a);
    if (a == Action::Start) stop_hold_since_.reset();
}

void ShowController::buttonDown(Action a, double now) {
    press(a, now);
    updateTimer();
}

void ShowController::buttonUp(Action a, double now) {
    release(a, now);
    updateTimer();
}

void ShowController::keyDown(int keyCode, int modifiers, double now) {
    last_time_ = now;
    const int code = sameCase(keyCode);
    const auto action = controls().actionForKey(code, modifiers);
    const bool esc = !action && code == juce::KeyPress::escapeKey;   // Esc, unless the band gave it to an action
    if (!action && !esc) return;                   // never tracked: no key-up is polled for it
    if (!keys_.emplace(code, action).second) return;   // a repeat of a key already held

    if (esc) {
        if (have_esc_ && now - esc_at_ <= doubleEscSeconds) { have_esc_ = false;  act(Action::Stop); }
        else { have_esc_ = true;  esc_at_ = now; }
        return;
    }
    press(*action, now);
}

void ShowController::keyUp(int keyCode, double now) {
    last_time_ = now;
    const auto found = keys_.find(sameCase(keyCode));
    if (found == keys_.end()) return;
    const auto action = found->second;
    keys_.erase(found);
    if (action) release(*action, now);
}

void ShowController::keyDown(ShowKey key, double now) { keyDown(codeOf(key), 0, now); }
void ShowController::keyUp(ShowKey key, double now) { keyUp(codeOf(key), now); }

void ShowController::tick(double now) {
    last_time_ = now;
    if (stop_hold_since_ && now - *stop_hold_since_ >= holdSeconds) {
        stop_hold_since_.reset();                  // used: once only
        act(Action::Stop);
    }
}

void ShowController::keyboardLost() {
    for (auto& [code, action] : keys_)
        if (action) {
            held_.erase(*action);
            if (*action == Action::Start) stop_hold_since_.reset();
        }
    keys_.clear();                                 // a held pedal stays held: it doesn't need the keyboard
}

double ShowController::holdProgress() const {
    if (!stop_hold_since_) return 0.0;
    return std::clamp((last_time_ - *stop_hold_since_) / holdSeconds, 0.0, 1.0);
}

bool ShowController::keyPressed(const juce::KeyPress& k, juce::Component*) {
    keyDown(k.getKeyCode(), k.getModifiers().getRawFlags(), clockSeconds());
    updateTimer();
    return true;
}

bool ShowController::keyStateChanged(bool, juce::Component*) {
    releaseVanishedKeys(clockSeconds());
    updateTimer();
    return true;
}

// A key the system no longer reports as down was let go (or the keyboard was taken away): treat it as released. Only keys:
// a pedal held down has no key to look at, and is let go by its own release.
void ShowController::releaseVanishedKeys(double now) {
    std::vector<int> held;
    for (auto& entry : keys_) held.push_back(entry.first);
    for (int code : held)
        if (!juce::KeyPress::isKeyCurrentlyDown(code)) keyUp(code, now);
}

void ShowController::timerCallback() {
    const auto now = clockSeconds();
    releaseVanishedKeys(now);
    tick(now);
    updateTimer();
}

void ShowController::updateTimer() {
    if (keys_.empty() && held_.empty()) { if (isTimerRunning()) stopTimer(); }
    else if (!isTimerRunning()) startTimerHz(30);
}

}  // namespace sqlive::app
