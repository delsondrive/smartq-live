#include "app/ShowController.h"

#include <algorithm>

namespace sqlive::app {

namespace {
double clockSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

struct KeyCode { ShowKey key; int code; };
const KeyCode trackedKeys[] = {
    { ShowKey::Space, juce::KeyPress::spaceKey },   { ShowKey::Escape, juce::KeyPress::escapeKey },
    { ShowKey::Left, juce::KeyPress::leftKey },     { ShowKey::Right, juce::KeyPress::rightKey },
    { ShowKey::Tab, juce::KeyPress::tabKey },
};

ShowKey translate(const juce::KeyPress& k) {
    for (auto& t : trackedKeys)
        if (k.getKeyCode() == t.code) return t.key;
    return ShowKey::Other;
}
}  // namespace

ShowController::ShowController(Session& s) : session_(s) {
    onAction = [this](Action a) { session_.perform(a); };
}

ShowController::~ShowController() { stopTimer(); }

void ShowController::act(Action a) {
    if (onAction) onAction(a);
}

void ShowController::keyDown(ShowKey key, double now) {
    last_time_ = now;
    if (!down_.insert(key).second) return;         // a repeat of a key already held

    switch (key) {
        case ShowKey::Space:
            if (!session_.now().playing) act(Action::Start);       // this press is used: holding on never stops the song
            else { space_holding_ = true;  space_since_ = now; }
            break;
        case ShowKey::Escape:
            if (have_esc_ && now - esc_at_ <= doubleEscSeconds) { have_esc_ = false;  act(Action::Stop); }
            else { have_esc_ = true;  esc_at_ = now; }
            break;
        case ShowKey::Left:  act(Action::NextSongEarlier); break;
        case ShowKey::Right: act(Action::NextSongLater); break;
        case ShowKey::Tab:   act(Action::ToggleView); break;
        case ShowKey::Other: break;
    }
}

void ShowController::keyUp(ShowKey key, double now) {
    last_time_ = now;
    down_.erase(key);
    if (key == ShowKey::Space) space_holding_ = false;
}

void ShowController::tick(double now) {
    last_time_ = now;
    if (space_holding_ && now - space_since_ >= holdSeconds) {
        space_holding_ = false;                    // used: once only
        act(Action::Stop);
    }
}

void ShowController::keyboardLost() {
    down_.clear();
    space_holding_ = false;
}

double ShowController::holdProgress() const {
    if (!space_holding_) return 0.0;
    return std::clamp((last_time_ - space_since_) / holdSeconds, 0.0, 1.0);
}

bool ShowController::keyPressed(const juce::KeyPress& k, juce::Component*) {
    keyDown(translate(k), clockSeconds());
    updateTimer();
    return true;
}

bool ShowController::keyStateChanged(bool, juce::Component*) {
    const auto now = clockSeconds();
    const auto held = down_;
    for (auto& t : trackedKeys)
        if (held.count(t.key) && !juce::KeyPress::isKeyCurrentlyDown(t.code)) keyUp(t.key, now);
    updateTimer();
    return true;
}

void ShowController::timerCallback() {
    tick(clockSeconds());
    updateTimer();
}

void ShowController::updateTimer() {
    if (down_.empty()) { if (isTimerRunning()) stopTimer(); }
    else if (!isTimerRunning()) startTimerHz(30);
}

}  // namespace sqlive::app
