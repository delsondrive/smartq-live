#include "model/Resampler.h"

#include <algorithm>
#include <cmath>

namespace sqlive::model {

namespace {

constexpr int kBlockFrames = 65536;

// The low-pass in front of a downsample: a Kaiser-windowed sinc, its cutoff at 0.45 of the new rate. kTapsPerSpeed taps on
// each side for every input sample an output steps over keeps the transition band the same width at the new rate, and a
// beta of 10 puts the stop band about 100 dB down, so what is above the new Nyquist frequency is gone before it can fold back.
constexpr double kCutoff = 0.45;
constexpr int kTapsPerSpeed = 32;
constexpr double kKaiserBeta = 10.0;

double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 64 && term > 1e-12 * sum; ++k) {
        const double t = x / (2.0 * k);
        term *= t * t;
        sum += term;
    }
    return sum;
}

std::vector<double> lowPassTaps(double speed) {
    const int half = (int) std::ceil(kTapsPerSpeed * speed);
    const double fc = kCutoff / speed;   // in cycles per input sample
    std::vector<double> taps((size_t) (2 * half + 1));
    double sum = 0.0;
    for (int n = -half; n <= half; ++n) {
        const double x = 2.0 * fc * n;
        const double sinc = n == 0 ? 1.0 : std::sin(juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
        const double r = (double) n / (double) half;
        const double window = besselI0(kKaiserBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / besselI0(kKaiserBeta);
        taps[(size_t) (n + half)] = 2.0 * fc * sinc * window;
        sum += taps[(size_t) (n + half)];
    }
    for (auto& t : taps) t /= sum;   // a gain of exactly 1 at 0 Hz
    return taps;
}

}  // namespace

Resampler::Resampler(int channels, double speed)
    : speed_(speed), interpolators_((size_t) channels), pending_((size_t) channels) {
    if (speed > 1.0) {
        taps_ = lowPassTaps(speed);
        history_.assign((size_t) channels, std::vector<float>(taps_.size() - 1, 0.0f));
        delay_left_ = (int) (taps_.size() / 2);
    }
}

void Resampler::filterInto(size_t channel, const float* input, int frames) {
    // history_ holds the inputs before this block, so every output sees the same inputs however the stream is split.
    auto& history = history_[channel];
    const size_t length = taps_.size(), half = length / 2;
    window_.assign(history.begin(), history.end());
    window_.insert(window_.end(), input, input + frames);
    const int skip = std::min(delay_left_, frames);   // the filter's delay: outputs before the first input's centre
    for (int i = skip; i < frames; ++i) {
        const float* x = window_.data() + i;          // x[0 .. length) are the inputs this output weighs
        double y = taps_[half] * x[half];
        for (size_t k = 0; k < half; ++k) y += taps_[k] * ((double) x[k] + (double) x[length - 1 - k]);   // symmetric
        pending_[channel].push_back((float) y);
    }
    history.assign(window_.end() - (std::ptrdiff_t) (length - 1), window_.end());
}

void Resampler::push(const juce::AudioBuffer<float>& input, int frames) {
    for (size_t c = 0; c < pending_.size(); ++c) {
        const float* p = input.getReadPointer((int) c);
        if (taps_.empty()) pending_[c].insert(pending_[c].end(), p, p + frames);
        else filterInto(c, p, frames);
    }
    if (! taps_.empty()) delay_left_ -= std::min(delay_left_, frames);
}

void Resampler::pushSilence(int frames) {
    if (taps_.empty()) {
        for (auto& p : pending_) p.insert(p.end(), (size_t) frames, 0.0f);
        return;
    }
    const std::vector<float> silence((size_t) frames, 0.0f);   // through the filter, which still holds the input's tail
    for (size_t c = 0; c < pending_.size(); ++c) filterInto(c, silence.data(), frames);
    delay_left_ -= std::min(delay_left_, frames);
}
int Resampler::pull(juce::AudioBuffer<float>& out, int maxFrames) {
    if (! primed_) {
        float discarded = 0.0f;
        if (run(kLatency, nullptr, &discarded, 1) == 0) return 0;
        primed_ = true;
    }
    return run(speed_, &out, nullptr, maxFrames);
}

int Resampler::run(double speed, juce::AudioBuffer<float>* out, float* scratch, int maxFrames) {
    // Count them as the interpolator will: before each output it takes one input for every whole step of its position.
    const int available = pending_.empty() ? 0 : (int) pending_[0].size();
    double pos = pos_;
    int made = 0, used = 0;
    while (made < maxFrames) {
        double next = pos;
        int need = 0;
        while (next >= 1.0) { next -= 1.0; ++need; }
        if (used + need > available) break;
        used += need;
        pos = next + speed;
        ++made;
    }
    if (made == 0) return 0;
    for (size_t c = 0; c < pending_.size(); ++c) {
        float* dest = out != nullptr ? out->getWritePointer((int) c) : scratch;
        [[maybe_unused]] const int taken = interpolators_[c].process(speed, pending_[c].data(), dest, made);
        jassert(taken == used);
        pending_[c].erase(pending_[c].begin(), pending_[c].begin() + used);
    }
    pos_ = pos;
    return made;
}

ReadEnd readAtRate(juce::AudioFormatReader& reader, int channels, double targetRate,
                   const std::function<bool(double progress)>& keepGoing,
                   const std::function<bool(const juce::AudioBuffer<float>& block, int frames)>& sink) {
    const juce::int64 inFrames = reader.lengthInSamples;
    const bool resample = reader.sampleRate != targetRate;
    const juce::int64 outFrames = resample ? std::llround((double) inFrames * targetRate / reader.sampleRate) : inFrames;

    juce::AudioBuffer<float> in(channels, kBlockFrames), out(channels, kBlockFrames);
    Resampler resampler(channels, reader.sampleRate / targetRate);
    juce::int64 readFrames = 0, made = 0;
    const auto drain = [&]() -> bool {
        while (made < outFrames) {
            const int frames = resampler.pull(out, (int) std::min<juce::int64>(kBlockFrames, outFrames - made));
            if (frames == 0) return true;
            if (! sink(out, frames)) return false;
            made += frames;
        }
        return true;
    };

    while (readFrames < inFrames) {
        const int frames = (int) std::min<juce::int64>(kBlockFrames, inFrames - readFrames);
        if (! reader.read(&in, 0, frames, readFrames, true, true)) return ReadEnd::ReadFailed;
        readFrames += frames;
        if (resample) {
            resampler.push(in, frames);
            if (! drain()) return ReadEnd::SinkFailed;
        } else {
            if (! sink(in, frames)) return ReadEnd::SinkFailed;
            made += frames;
        }
        if (! keepGoing((double) readFrames / (double) inFrames)) return ReadEnd::Stopped;
    }
    // The last outputs look ahead past the end of the input, into silence.
    while (made < outFrames) {
        resampler.pushSilence(256);
        if (! drain()) return ReadEnd::SinkFailed;
    }
    return ReadEnd::Done;
}

}  // namespace sqlive::model
