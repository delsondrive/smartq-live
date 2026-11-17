#include "engine/Arrangement.h"

#include <algorithm>

namespace sqlive::engine {

void Arrangement::reset(const PreparedSong* song) noexcept {
    song_ = song;
    whole_ = {0, song != nullptr ? song->end : 0};
    step_ = -1;
    over_ = false;
    first_ = 0;
    lead_end_ = 0;
    pending_ = -1;
    looping_ = false;
    early_ = false;
}

void Arrangement::leadInto(int step, SamplePos at) noexcept {
    if (step < 0 || step >= stepCount()) return;
    first_ = step;
    lead_end_ = at;
}

int Arrangement::step() const noexcept { return step_; }

void Arrangement::enter(int step) noexcept {
    const bool leaving = step_ >= 0;   // from a step, rather than from the lead-in
    step_ = step >= 0 && step < stepCount() ? step : -1;
    over_ = step_ < 0;
    if (leaving || over_) pending_ = -1;
    if (over_) looping_ = false;
    early_ = false;
}

void Arrangement::choose(int step) noexcept {
    if (song_ == nullptr || over_) return;
    if (step == -1) {
        pending_ = -1;
        looping_ = false;
        early_ = false;
        return;
    }
    if (step < 0 || step >= stepCount()) return;
    pending_ = step;
    looping_ = false;
    early_ = step_ >= 0;
}

void Arrangement::setLoop(bool on) noexcept {
    if (song_ == nullptr || over_) return;
    looping_ = on;
    if (on) {
        pending_ = -1;
        early_ = step_ >= 0;
    } else if (pending_ < 0) {
        early_ = false;
    }
}

int Arrangement::pending() const noexcept { return pending_; }
bool Arrangement::looping() const noexcept { return looping_; }

int Arrangement::nextStep() const noexcept {
    if (song_ == nullptr || over_) return -1;
    if (step_ < 0) return first_;
    if (looping_) return step_;
    if (pending_ >= 0) return pending_;
    const int next = step_ + 1;
    return next < stepCount() ? next : -1;
}

SamplePos Arrangement::boundary(SamplePos songTime) const noexcept {
    if (step_ < 0) return lead_end_;
    const SamplePos end = sectionOf(step_).end;
    if (song_->jumpTiming != JumpTiming::NextBar || !early_ || (pending_ < 0 && !looping_)) return end;
    const auto& bars = song_->barStarts;
    const auto next = std::upper_bound(bars.begin(), bars.end(), songTime);
    return next != bars.end() && *next < end ? *next : end;
}

int Arrangement::stepAt(SamplePos songTime) const noexcept {
    const int count = stepCount();
    for (int i = 0; i < count; ++i) {
        const Section& s = sectionOf(i);
        if (s.start <= songTime && songTime < s.end) return i;
    }
    for (int i = 0; i < count; ++i)
        if (sectionOf(i).start > songTime) return i;
    return -1;
}

const Section& Arrangement::sectionOf(int step) const noexcept {
    if (!usesVectors() || step < 0 || step >= stepCount()) return whole_;
    const std::uint16_t index = song_->arrangement[static_cast<std::size_t>(step)];
    return index < song_->sections.size() ? song_->sections[index] : whole_;
}

int Arrangement::stepCount() const noexcept {
    if (song_ == nullptr) return 0;
    return usesVectors() ? static_cast<int>(song_->arrangement.size()) : 1;
}

bool Arrangement::usesVectors() const noexcept {
    return song_ != nullptr && !song_->sections.empty() && !song_->arrangement.empty();
}

} // namespace sqlive::engine
