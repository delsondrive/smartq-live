#include "app/PreparedStore.h"

#include <algorithm>
#include <utility>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>

#include "app/Settings.h"
#include "model/AtomicFile.h"
#include "model/Effects.h"

namespace sqlive::app {

namespace {
constexpr const char* kExtension = ".wav";
constexpr int kBlockFrames = 65536;
}  // namespace

PreparedStore::PreparedStore(juce::File folder, std::int64_t limitBytes) : folder_(std::move(folder)), limit_(limitBytes) {}

juce::File PreparedStore::defaultFolder() { return Settings::defaultFile().getParentDirectory().getChildFile("Prepared audio"); }

void PreparedStore::setLimit(std::int64_t bytes) { limit_.store(bytes); }

juce::String PreparedStore::keyFor(const juce::File& partFile, const std::vector<model::EffectDoc>& effects, double sampleRate) {
    juce::String text;
    text << partFile.getFullPathName() << "\n" << partFile.getSize() << "\n" << partFile.getLastModificationTime().toMilliseconds()
         << "\n" << model::effectsKey(effects) << "\n" << juce::String(sampleRate, 3) << "\n" << model::kEffectsCodeVersion;
    return juce::SHA256(text.toUTF8(), text.getNumBytesAsUTF8()).toHexString();
}

juce::File PreparedStore::fileFor(const juce::String& key) const { return folder_.getChildFile(key + kExtension); }

bool PreparedStore::has(const juce::String& key) const { return fileFor(key).existsAsFile(); }

std::shared_ptr<const engine::PartAudio> PreparedStore::read(const juce::String& key,
                                                             const std::function<bool(double)>& keepGoing) const {
    const auto file = fileFor(key);
    if (! file.existsAsFile()) return nullptr;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(file.createInputStream().release(), true));
    if (reader == nullptr || reader->numChannels < 1 || reader->numChannels > 2 || reader->lengthInSamples <= 0) return nullptr;

    const auto frames = (std::size_t) reader->lengthInSamples;
    const bool stereo = reader->numChannels == 2;
    auto audio = std::make_shared<engine::PartAudio>();
    audio->left.resize(frames);
    if (stereo) audio->right.resize(frames);
    for (std::size_t at = 0; at < frames; at += kBlockFrames) {
        const auto count = (int) std::min<std::size_t>(kBlockFrames, frames - at);
        float* dest[2] = {audio->left.data() + at, stereo ? audio->right.data() + at : nullptr};
        if (! reader->read(dest, (int) reader->numChannels, (juce::int64) at, count)) return nullptr;
        if (keepGoing && ! keepGoing((double) (at + (std::size_t) count) / (double) frames)) return nullptr;
    }
    file.setLastModificationTime(juce::Time::getCurrentTime());        // used now: trim drops the others first
    return audio;
}

bool PreparedStore::write(const juce::String& key, const engine::PartAudio& audio) {
    const auto frames = audio.left.size();
    if (frames == 0 || ! folder_.createDirectory().wasOk()) return false;
    const bool stereo = audio.right.size() == frames;
    const auto target = fileFor(key);
    const auto temp = folder_.getChildFile(key + ".tmp");
    temp.deleteFile();
    bool written = false;
    {
        std::unique_ptr<juce::OutputStream> stream = temp.createOutputStream();
        if (stream == nullptr) return false;
        juce::WavAudioFormat wav;
        // The sample rate is not read back (the key holds it), so any value will do; 48000 keeps the header valid.
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                      .withSampleRate(48000.0)
                                                      .withNumChannels(stereo ? 2 : 1)
                                                      .withBitsPerSample(32)
                                                      .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        if (writer != nullptr) {
            written = true;
            for (std::size_t at = 0; at < frames && written; at += kBlockFrames) {
                const auto count = (int) std::min<std::size_t>(kBlockFrames, frames - at);
                const float* channels[2] = {audio.left.data() + at, stereo ? audio.right.data() + at : nullptr};
                written = writer->writeFromFloatArrays(channels, stereo ? 2 : 1, count);
            }
            writer.reset();                                             // writes the header and closes the file
        }
    }
    if (! written || ! model::moveRetrying(temp, target)) {
        temp.deleteFile();
        return false;
    }
    return true;
}

std::int64_t PreparedStore::sizeBytes() const {
    std::int64_t total = 0;
    for (const auto& entry : juce::RangedDirectoryIterator(folder_, false, juce::String("*") + kExtension, juce::File::findFiles))
        total += entry.getFile().getSize();
    return total;
}

void PreparedStore::clear() {
    for (const auto& entry : juce::RangedDirectoryIterator(folder_, false, "*", juce::File::findFiles)) {
        const auto name = entry.getFile().getFileName();
        if (name.endsWith(kExtension) || name.endsWith(".tmp")) entry.getFile().deleteFile();
    }
}

void PreparedStore::trim() {
    struct Item { juce::File file; juce::int64 size; juce::int64 used; };
    std::vector<Item> items;
    std::int64_t total = 0;
    for (const auto& entry : juce::RangedDirectoryIterator(folder_, false, juce::String("*") + kExtension, juce::File::findFiles)) {
        const auto file = entry.getFile();
        items.push_back({file, file.getSize(), file.getLastModificationTime().toMilliseconds()});
        total += items.back().size;
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.used < b.used; });
    for (const auto& item : items) {
        if (total <= limit_.load()) break;
        if (item.file.deleteFile()) total -= item.size;
    }
}

}  // namespace sqlive::app
