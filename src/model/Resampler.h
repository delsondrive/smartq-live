#pragma once

#include <functional>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

namespace sqlive::model {

// Feeds one channel's WindowedSincInterpolator exactly the input its next outputs need, so the stream has no seams between
// blocks. Output m is the input at m * speed: the interpolator's 100-sample latency is taken up by a first output thrown away.
// Downsampling isn't low-pass filtered yet: content above the new Nyquist frequency can alias.
class Resampler {
public:
    Resampler(int channels, double speed);

    void push(const juce::AudioBuffer<float>& input, int frames);
    void pushSilence(int frames);
    // Up to maxFrames of output into `out`, as many as the input pushed so far allows.
    int pull(juce::AudioBuffer<float>& out, int maxFrames);

private:
    static constexpr double latency_ = (double) juce::WindowedSincInterpolator::getBaseLatency();

    // Makes up to maxFrames outputs at `speed` from the pending input, into `out`, or (one frame to throw away) into `scratch`.
    int run(double speed, juce::AudioBuffer<float>* out, float* scratch, int maxFrames);

    double speed_, pos_ = 1.0;   // pos_ mirrors each interpolator's own position, which starts at 1 after a reset
    bool primed_ = false;
    std::vector<juce::WindowedSincInterpolator> interpolators_;
    std::vector<std::vector<float>> pending_;
};

enum class ReadEnd { Done, ReadFailed, SinkFailed, Stopped };

// Reads the first `channels` channels of all of `reader` at `targetRate` (resampled when its rate differs: then
// round(length * targetRate / rate) frames), in blocks of up to 65536 frames handed to `sink` in order. After each block read
// it calls `keepGoing` with how far it has got (0..1); false stops it. `sink` returns false to stop with SinkFailed.
ReadEnd readAtRate(juce::AudioFormatReader& reader, int channels, double targetRate,
                   const std::function<bool(double progress)>& keepGoing,
                   const std::function<bool(const juce::AudioBuffer<float>& block, int frames)>& sink);

}  // namespace sqlive::model
