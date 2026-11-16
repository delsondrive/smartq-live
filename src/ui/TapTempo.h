#pragma once

#include <deque>
#include <optional>

namespace sqlive::ui {

// Tap tempo: the BPM once there are 3 taps less than 2 s apart, from the average of the last few gaps. A tap 2 s or more
// after the one before (or before it) starts again; only the last 8 taps count, so older ones are forgotten.
class TapTempo {
public:
    static constexpr double maxGapSeconds = 2.0;
    static constexpr std::size_t keptTaps = 8;

    std::optional<double> tap(double nowSeconds) {
        if (!taps_.empty() && (nowSeconds - taps_.back() >= maxGapSeconds || nowSeconds <= taps_.back())) taps_.clear();
        taps_.push_back(nowSeconds);
        while (taps_.size() > keptTaps) taps_.pop_front();
        if (taps_.size() < 3) return std::nullopt;
        const double gap = (taps_.back() - taps_.front()) / static_cast<double>(taps_.size() - 1);
        return 60.0 / gap;
    }

    bool started() const { return !taps_.empty(); }

private:
    std::deque<double> taps_;
};

}  // namespace sqlive::ui
