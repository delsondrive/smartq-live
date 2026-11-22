#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <juce_events/juce_events.h>

#include "app/Settings.h"

namespace sqlive::app {

// Who may use the phone remote of one band folder: the PIN a new phone types, the phones that joined and how far each is
// let in, and the addresses locked out after wrong PINs. Thread-safe: the web threads and the message thread both call it.
// Every change bumps version() and sends a change message from the message thread (a change made on a web thread is
// posted there, behind a token that dies with this); a change worth keeping is written to Settings and saved, on the
// message thread too (at once when called there, posted otherwise). A phone's token is never kept, only its SHA-256.
class RemoteAccess : public juce::ChangeBroadcaster {
public:
    static constexpr int maxPhones = 16;
    static constexpr int maxNameLength = 40;

    RemoteAccess(Settings& settings, juce::File bandRoot);
    ~RemoteAccess() override;

    juce::String pin() const;                                           // four digits
    void newPin();                                                      // a different four digits; phones already joined keep their access

    bool enabled() const;
    void setEnabled(bool on);
    int port() const;
    bool setPort(int port);                                             // false, and nothing changes, outside 1...65535

    // 200 with a token; 403 wrong PIN; 429 locked out, or 16 phones known and none of them both waiting and unconnected
    // (when one is, the oldest such is forgotten to make room); 400 no usable name. message says why, for the
    // phone to show. Five wrong PINs from one address within 60 s lock it out for 60 s from the last one. Even with the
    // right PIN, a sixth join from one address within 60 s, or a twenty-first from all addresses together, is refused (429)
    // and changes and saves nothing: a sliding window over the nowSeconds given, counting joins that were let through.
    struct JoinResult { int status = 400; juce::String token, message; };
    JoinResult join(const juce::String& name, const juce::String& pin, const juce::String& address, double nowSeconds);

    PhoneAccess accessFor(const juce::String& token) const;             // Refused for an unknown token
    juce::String phoneIdFor(const juce::String& token) const;           // "" for an unknown token
    std::vector<Phone> phones() const;                                  // in the order they joined

    // Lets a phone in (ViewOnly, FullControl) or refuses it. Refused forgets the phone, so it can join again only with the
    // PIN and is let in again only from the laptop. A waiting phone is let in only in Edit mode; a phone already let in can
    // be changed or refused at any time. False when nothing could be done: an unknown phone, Waiting asked for, or a waiting
    // phone let in outside Edit mode.
    bool decide(const juce::String& phoneId, PhoneAccess access, bool appIsInEditMode);
    void forget(const juce::String& phoneId);
    void setConnected(const juce::String& phoneId, bool connected);    // not kept across restarts

    std::uint64_t version() const;                                      // goes up with every change

private:
    struct Attempts { std::vector<double> wrong; double lockedUntil = -1.0; };

    void changed(bool keepIt);                                          // after a change, with the lock released
    void keep();                                                        // saves now on the message thread, or posts the save there
    void saveNow();                                                     // message thread
    int indexOfLocked(const juce::String& phoneId) const;
    int indexOfTokenLocked(const juce::String& token) const;
    void eraseLocked(int index);
    void forgetOldAttemptsLocked(double nowSeconds);

    Settings& settings_;
    const juce::File bandRoot_;

    mutable std::mutex mutex_;
    RemoteMemory memory_;                                               // under mutex_
    std::map<juce::String, Attempts> attempts_;                         // by address, under mutex_
    std::map<juce::String, std::vector<double>> joins_;                 // when each address joined within the last minute
    std::vector<double> allJoins_;                                      // the same, all addresses together (under mutex_)

    std::atomic<std::uint64_t> version_{0};
    std::atomic<bool> unsaved_{false};                                  // a change Settings doesn't have yet
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);             // what posted saves check, so none runs after this has gone
};

}  // namespace sqlive::app
