#include "engine/Arrangement.h"

namespace sqlive::engine {

void Arrangement::reset(const PreparedSong* song) noexcept {
    song_ = song;
    whole_ = {0, song != nullptr ? song->end : 0};
    step_ = -1;
    over_ = false;
}

int Arrangement::step() const noexcept { return step_; }

void Arrangement::enter(int step) noexcept {
    step_ = step >= 0 && step < stepCount() ? step : -1;
    over_ = step_ < 0;
}

int Arrangement::nextStep() const noexcept {
    if (song_ == nullptr || over_) return -1;
    const int next = step_ + 1;   // the lead-in's (-1) is step 0
    return next < stepCount() ? next : -1;
}

SamplePos Arrangement::boundary() const noexcept { return step_ >= 0 ? sectionOf(step_).end : 0; }

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
