#pragma once

#include <cmath>
#include <memory>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

// Audio files for tests: sine waves written as WAV, and WAV files read back as plain samples.
namespace sqlive::test {

// A sine of `hz` at `peak` on every channel, written as a `bits`-bit WAV (its folder is made). Returns the file.
inline juce::File writeSine(const juce::File& file, double sampleRate, int channels, double seconds, double hz, float peak, int bits = 24) {
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    const int frames = (int) std::llround(seconds * sampleRate);
    juce::AudioBuffer<float> buffer(channels, frames);
    for (int i = 0; i < frames; ++i) {
        const float v = peak * (float) std::sin(juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate);
        for (int c = 0; c < channels; ++c) buffer.setSample(c, i, v);
    }
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                  .withSampleRate(sampleRate)
                                                  .withNumChannels(channels)
                                                  .withBitsPerSample(bits));
    if (writer != nullptr) writer->writeFromAudioSampleBuffer(buffer, 0, frames);
    return file;
}

// Every channel of a WAV file; empty when it can't be read. `sampleRate`, when given, is set to the file's rate.
inline std::vector<std::vector<float>> readWav(const juce::File& file, double* sampleRate = nullptr) {
    auto input = file.createInputStream();
    if (input == nullptr) return {};
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(input.release(), true));
    if (reader == nullptr) return {};
    if (sampleRate != nullptr) *sampleRate = reader->sampleRate;
    const int frames = (int) reader->lengthInSamples, channels = (int) reader->numChannels;
    juce::AudioBuffer<float> buffer(channels, frames);
    reader->read(&buffer, 0, frames, 0, true, true);
    std::vector<std::vector<float>> out((size_t) channels);
    for (int c = 0; c < channels; ++c) out[(size_t) c].assign(buffer.getReadPointer(c), buffer.getReadPointer(c) + frames);
    return out;
}

}  // namespace sqlive::test
