#include "app/RemoteAccess.h"

#include <algorithm>
#include <random>
#include <set>

#include <juce_cryptography/juce_cryptography.h>

namespace sqlive::app {

namespace {
constexpr int tokenBytes = 32;
constexpr int idBytes = 8;
constexpr int wrongPinsToLock = 5;
constexpr double lockWindowSeconds = 60.0;
constexpr int maxAddressLength = 64;                                    // an IPv6 address is at most 45 characters
constexpr int maxNameInput = 1000;                                      // characters looked at before cutting to 40

const char* const hexDigits = "0123456789abcdef";

// std::random_device is the system's cryptographic generator on MSVC; juce::Random would make tokens predictable. One per
// call, so no two threads share one.
juce::String randomHex(int bytes) {
    std::random_device device;
    std::vector<unsigned char> data(static_cast<size_t>(bytes));
    for (auto& byte : data) byte = static_cast<unsigned char>(device() & 0xff);
    return juce::String::toHexString(data.data(), bytes, 0);
}

juce::String randomPin() {
    std::random_device device;
    return juce::String(std::uniform_int_distribution<int>(0, 9999)(device)).paddedLeft('0', 4);
}

bool isPin(const juce::String& text) { return text.length() == 4 && text.containsOnly("0123456789"); }

bool isHex(const juce::String& text, int length) { return text.length() == length && text.containsOnly(hexDigits); }

juce::String hashOf(const juce::String& token) { return juce::SHA256(token.toUTF8()).toHexString(); }

// Trimmed, without control characters (C0, DEL, C1, the line and paragraph separators), at most 40 characters.
juce::String saneName(const juce::String& raw) {
    std::vector<juce::juce_wchar> kept;
    int looked = 0;
    for (auto p = raw.getCharPointer(); !p.isEmpty() && looked < maxNameInput; ++looked) {
        const auto c = p.getAndAdvance();
        if (c < 0x20 || (c >= 0x7f && c <= 0x9f) || c == 0x2028 || c == 0x2029) continue;
        kept.push_back(c);
    }
    kept.push_back(0);
    return juce::String(juce::CharPointer_UTF32(kept.data())).trim().substring(0, RemoteAccess::maxNameLength).trimEnd();
}
}  // namespace

RemoteAccess::RemoteAccess(Settings& settings, juce::File bandRoot) : settings_(settings), bandRoot_(std::move(bandRoot)) {
    const auto loaded = settings_.remoteMemory(bandRoot_);
    bool repaired = false;

    memory_.enabled = loaded.enabled;
    memory_.pin = loaded.pin;
    if (!isPin(memory_.pin)) { memory_.pin = randomPin();  repaired = true; }
    memory_.port = loaded.port;
    if (memory_.port < 1 || memory_.port > 65535) { memory_.port = 8080;  repaired = true; }

    // A file edited by hand may hold anything: keep only phones that could have been written here.
    std::set<juce::String> ids, hashes;
    for (size_t i = 0; i < loaded.phones.size(); ++i) {
        auto phone = loaded.phones[i];
        const auto hash = loaded.tokenHashes[static_cast<int>(i)];
        const bool usable = phone.access != PhoneAccess::Refused && isHex(hash, 64) && ids.count(phone.id) == 0
                            && hashes.count(hash) == 0 && static_cast<int>(memory_.phones.size()) < maxPhones;
        if (!usable) { repaired = true;  continue; }
        phone.connected = false;
        phone.name = saneName(phone.name);
        if (phone.name.isEmpty()) phone.name = "Phone";
        ids.insert(phone.id);
        hashes.insert(hash);
        memory_.phones.push_back(phone);
        memory_.tokenHashes.add(hash);
    }
    if (repaired) keep();
}

RemoteAccess::~RemoteAccess() {
    alive_.reset();                                                     // saves already posted do nothing now
    if (unsaved_ && juce::MessageManager::existsAndIsCurrentThread()) saveNow();
}

juce::String RemoteAccess::pin() const {
    std::lock_guard lock(mutex_);
    return memory_.pin;
}

void RemoteAccess::newPin() {
    {
        std::lock_guard lock(mutex_);
        auto pin = randomPin();
        while (pin == memory_.pin) pin = randomPin();
        memory_.pin = pin;
    }
    changed(true);
}

bool RemoteAccess::enabled() const {
    std::lock_guard lock(mutex_);
    return memory_.enabled;
}

void RemoteAccess::setEnabled(bool on) {
    {
        std::lock_guard lock(mutex_);
        if (memory_.enabled == on) return;
        memory_.enabled = on;
    }
    changed(true);
}

int RemoteAccess::port() const {
    std::lock_guard lock(mutex_);
    return memory_.port;
}

bool RemoteAccess::setPort(int port) {
    if (port < 1 || port > 65535) return false;
    {
        std::lock_guard lock(mutex_);
        if (memory_.port == port) return true;
        memory_.port = port;
    }
    changed(true);
    return true;
}

void RemoteAccess::forgetOldAttemptsLocked(double nowSeconds) {
    for (auto it = attempts_.begin(); it != attempts_.end();) {
        auto& wrong = it->second.wrong;
        wrong.erase(std::remove_if(wrong.begin(), wrong.end(), [&](double t) { return t < nowSeconds - lockWindowSeconds; }),
                    wrong.end());
        if (wrong.empty() && nowSeconds >= it->second.lockedUntil) it = attempts_.erase(it);
        else ++it;
    }
}

RemoteAccess::JoinResult RemoteAccess::join(const juce::String& name, const juce::String& pin, const juce::String& address,
                                            double nowSeconds) {
    JoinResult result;
    {
        std::lock_guard lock(mutex_);
        forgetOldAttemptsLocked(nowSeconds);
        const auto from = address.substring(0, maxAddressLength);
        if (const auto it = attempts_.find(from); it != attempts_.end() && nowSeconds < it->second.lockedUntil)
            return {429, {}, "Too many wrong PINs. Try again in a minute."};

        if (!isPin(pin) || pin != memory_.pin) {
            auto& attempts = attempts_[from];
            attempts.wrong.push_back(nowSeconds);
            if (static_cast<int>(attempts.wrong.size()) >= wrongPinsToLock) {
                attempts.lockedUntil = nowSeconds + lockWindowSeconds;
                attempts.wrong.clear();
            }
            return {403, {}, "That PIN isn't right. Check it on the laptop."};
        }
        attempts_.erase(from);                                          // a right PIN starts the count again

        const auto phoneName = saneName(name);
        if (phoneName.isEmpty()) return {400, {}, "Give this phone a name."};
        if (static_cast<int>(memory_.phones.size()) >= maxPhones)
            return {429, {}, "Too many phones are waiting. Ask at the laptop."};

        Phone phone;
        phone.name = phoneName;
        do phone.id = randomHex(idBytes);
        while (indexOfLocked(phone.id) >= 0);
        result.token = randomHex(tokenBytes);
        memory_.phones.push_back(phone);
        memory_.tokenHashes.add(hashOf(result.token));
        result.status = 200;
        result.message = "Waiting for someone at the laptop to let this phone in.";
    }
    changed(true);
    return result;
}

int RemoteAccess::indexOfLocked(const juce::String& phoneId) const {
    for (size_t i = 0; i < memory_.phones.size(); ++i)
        if (memory_.phones[i].id == phoneId) return static_cast<int>(i);
    return -1;
}

int RemoteAccess::indexOfTokenLocked(const juce::String& token) const {
    if (!isHex(token, tokenBytes * 2)) return -1;                       // nothing else was ever handed out: not worth hashing
    return memory_.tokenHashes.indexOf(hashOf(token));
}

PhoneAccess RemoteAccess::accessFor(const juce::String& token) const {
    std::lock_guard lock(mutex_);
    const int index = indexOfTokenLocked(token);
    return index < 0 ? PhoneAccess::Refused : memory_.phones[static_cast<size_t>(index)].access;
}

juce::String RemoteAccess::phoneIdFor(const juce::String& token) const {
    std::lock_guard lock(mutex_);
    const int index = indexOfTokenLocked(token);
    return index < 0 ? juce::String() : memory_.phones[static_cast<size_t>(index)].id;
}

std::vector<Phone> RemoteAccess::phones() const {
    std::lock_guard lock(mutex_);
    return memory_.phones;
}

bool RemoteAccess::decide(const juce::String& phoneId, PhoneAccess access, bool appIsInEditMode) {
    if (access == PhoneAccess::Waiting) return false;
    {
        std::lock_guard lock(mutex_);
        const int index = indexOfLocked(phoneId);
        if (index < 0) return false;
        if (access == PhoneAccess::Refused) {
            eraseLocked(index);
        } else {
            auto& phone = memory_.phones[static_cast<size_t>(index)];
            if (phone.access == PhoneAccess::Waiting && !appIsInEditMode) return false;   // nobody joins during a show
            if (phone.access == access) return true;
            phone.access = access;
        }
    }
    changed(true);
    return true;
}

void RemoteAccess::eraseLocked(int index) {
    memory_.phones.erase(memory_.phones.begin() + index);
    memory_.tokenHashes.remove(index);
}

void RemoteAccess::forget(const juce::String& phoneId) {
    {
        std::lock_guard lock(mutex_);
        const int index = indexOfLocked(phoneId);
        if (index < 0) return;
        eraseLocked(index);
    }
    changed(true);
}

void RemoteAccess::setConnected(const juce::String& phoneId, bool connected) {
    {
        std::lock_guard lock(mutex_);
        const int index = indexOfLocked(phoneId);
        if (index < 0) return;
        auto& phone = memory_.phones[static_cast<size_t>(index)];
        if (phone.connected == connected) return;
        phone.connected = connected;
    }
    changed(false);
}

std::uint64_t RemoteAccess::version() const { return version_.load(); }

void RemoteAccess::changed(bool keepIt) {
    ++version_;
    if (keepIt) keep();
    sendChangeMessage();
}

void RemoteAccess::keep() {
    unsaved_ = true;
    if (juce::MessageManager::existsAndIsCurrentThread()) {
        saveNow();
        return;
    }
    std::weak_ptr<int> alive = alive_;
    juce::MessageManager::callAsync([this, alive] {
        if (alive.lock() != nullptr && unsaved_) saveNow();
    });
}

void RemoteAccess::saveNow() {
    unsaved_ = false;                                                   // before the copy: a change made after it posts again
    RemoteMemory copy;
    {
        std::lock_guard lock(mutex_);
        copy = memory_;
    }
    settings_.setRemoteMemory(bandRoot_, copy);
    if (!settings_.save().wasOk()) unsaved_ = true;                     // tried again with the next change, or on closing
}

}  // namespace sqlive::app
