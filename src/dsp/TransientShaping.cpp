#include "dsp/TransientShaping.h"

#include <algorithm>
#include <cmath>

namespace smartq::dsp {

void SlidingMax::allocate(int capacity) {
    capacity_ = capacity;
    values_.allocate(capacity);
    times_.allocate(capacity);
    clear();
}

void SlidingMax::clear() noexcept {
    front_ = 0;
    size_ = 0;
    now_ = 0;
}

double SlidingMax::push(double sample, int window) noexcept {
    // Out of the window from the front, and from the back whatever this sample is larger than, which can never be the largest again.
    while (size_ > 0 && static_cast<std::uint32_t>(now_ - times_[front_]) >= static_cast<std::uint32_t>(window)) {
        front_ = front_ + 1 == capacity_ ? 0 : front_ + 1;
        --size_;
    }
    const auto wrapped = [this](int index) { return index >= capacity_ ? index - capacity_ : index; };  // front_ and size_ are within the capacity
    while (size_ > 0 && values_[wrapped(front_ + size_ - 1)] <= sample) --size_;

    const int slot = wrapped(front_ + size_);
    values_[slot] = sample;
    times_[slot] = now_;
    ++size_;
    ++now_;
    return values_[front_];
}

void OnsetDetector::prepare(double sampleRate) {
    holdSamples_ = std::clamp(static_cast<int>(std::lround(transient::kHoldMs * 0.001 * sampleRate)), 1, transient::kMostHoldSamples);
    riseQuick_ = coefficientFor(transient::kQuickRiseMs, sampleRate);
    riseSpeed_ = coefficientFor(kSpeedMs, sampleRate);
    fallSpeed_ = riseSpeed_;
    held_.allocate(transient::kMostHoldSamples + 1);
    reset();
}

void OnsetDetector::reset() noexcept {
    held_.clear();
    fastAttack_ = 0.0;
    slowAttack_ = 0.0;
}

// As Transient's attack pair follow its held level.
void OnsetDetector::follow(double level) noexcept {
    const double held = held_.push(level, holdSamples_);
    fastAttack_ = transient::followed(fastAttack_, held, riseQuick_, fallSpeed_);
    slowAttack_ = transient::followed(slowAttack_, held, riseSpeed_, fallSpeed_);
}

}  // namespace smartq::dsp
