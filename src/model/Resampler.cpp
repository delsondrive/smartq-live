#include "model/Resampler.h"

#include <algorithm>
#include <cmath>

namespace sqlive::model {

namespace {
constexpr int kBlockFrames = 65536;
}  // namespace

Resampler::Resampler(int channels, double speed)
    : speed_(speed), interpolators_((size_t) channels), pending_((size_t) channels) {}

void Resampler::push(const juce::AudioBuffer<float>& input, int frames) {
    for (size_t c = 0; c < pending_.size(); ++c) {
        const float* p = input.getReadPointer((int) c);
        pending_[c].insert(pending_[c].end(), p, p + frames);
    }
}

void Resampler::pushSilence(int frames) {
    for (auto& p : pending_) p.insert(p.end(), (size_t) frames, 0.0f);
}

int Resampler::pull(juce::AudioBuffer<float>& out, int maxFrames) {
    if (! primed_) {
        float discarded = 0.0f;
        if (run(latency_, nullptr, &discarded, 1) == 0) return 0;
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
