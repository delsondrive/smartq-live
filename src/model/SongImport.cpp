#include "model/SongImport.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

#include "model/DocumentsJson.h"
#include "model/FileNames.h"

namespace sqlive::model {

namespace {

constexpr int kBlockFrames = 65536;

// Lower-cased runs of letters and digits.
juce::StringArray wordsOf(const juce::String& text) {
    juce::StringArray words;
    juce::String word;
    for (auto c : text) {
        if (juce::CharacterFunctions::isLetterOrDigit(c)) {
            word << juce::String::charToString(juce::CharacterFunctions::toLowerCase(c));
        } else if (word.isNotEmpty()) {
            words.add(word);
            word.clear();
        }
    }
    if (word.isNotEmpty()) words.add(word);
    return words;
}

struct RoleWords { const char* role; std::initializer_list<const char*> words; };
const RoleWords kRoleWords[] = {
    {"keys", {"keys", "key", "piano", "pno", "rhodes", "wurli", "organ", "ep"}},
    {"synth", {"synth", "syn", "lead", "arp", "seq"}},
    {"guitars", {"gtr", "gtrs", "guitar", "guitars", "ac", "acoustic"}},
    {"bass", {"bass", "sub"}},
    {"drums", {"drums", "drum", "kit", "kick", "snare", "hats"}},
    {"perc", {"perc", "percussion", "shaker", "tamb", "tambourine", "conga", "loop", "loops"}},
    {"bvs", {"bv", "bvs", "bgv", "bgvs", "vox", "vocals", "choir"}},
    {"strings", {"strings", "str", "orch"}},
    {"fx", {"fx", "sfx", "riser", "impact", "ambience"}},
};

const Role* partRoleNamed(const BandDoc& band, const juce::String& lowerName) {
    for (const auto& r : band.roles)
        if (r.kind == RoleKind::Part && r.name.toLowerCase() == lowerName) return &r;
    return nullptr;
}

bool isMidiFile(const juce::File& f) { return f.hasFileExtension(".mid;.midi"); }
bool hasAudioExtension(const juce::File& f) { return f.hasFileExtension(".wav;.aif;.aiff;.flac;.mp3;.ogg;.opus"); }

// Characters every name starts with, cut back to end at a non-letter and to leave something of every name.
int sharedLeadLength(const juce::StringArray& names) {
    if (names.size() < 2) return 0;
    int shared = names[0].length();
    for (const auto& n : names) {
        int i = 0;
        while (i < shared && i < n.length() && n[i] == names[0][i]) ++i;
        shared = i;
    }
    for (int len = shared; len > 0; --len) {
        if (juce::CharacterFunctions::isLetter(names[0][len - 1])) continue;
        bool leavesEveryName = true;
        for (const auto& n : names) leavesEveryName = leavesEveryName && n.substring(len).trim().isNotEmpty();
        if (leavesEveryName) return len;
    }
    return 0;
}

// Feeds one channel's WindowedSincInterpolator exactly the input its next outputs need, so the stream has no seams between
// blocks. Output m is the input at m * speed: the interpolator's 100-sample latency is taken up by a first output thrown away.
class Resampler {
public:
    Resampler(int channels, double speed) : speed_(speed), interpolators_((size_t) channels), pending_((size_t) channels) {}

    void push(const juce::AudioBuffer<float>& input, int frames) {
        for (size_t c = 0; c < pending_.size(); ++c) {
            const float* p = input.getReadPointer((int) c);
            pending_[c].insert(pending_[c].end(), p, p + frames);
        }
    }
    void pushSilence(int frames) {
        for (auto& p : pending_) p.insert(p.end(), (size_t) frames, 0.0f);
    }
    // Up to maxFrames of output into `out`, as many as the input pushed so far allows.
    int pull(juce::AudioBuffer<float>& out, int maxFrames) {
        if (! primed_) {
            float discarded = 0.0f;
            if (run(latency_, nullptr, &discarded, 1) == 0) return 0;
            primed_ = true;
        }
        return run(speed_, &out, nullptr, maxFrames);
    }

private:
    static constexpr double latency_ = (double) juce::WindowedSincInterpolator::getBaseLatency();

    // Makes up to maxFrames outputs at `speed` from the pending input, into `out`, or (one frame to throw away) into `scratch`.
    int run(double speed, juce::AudioBuffer<float>* out, float* scratch, int maxFrames) {
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

    double speed_, pos_ = 1.0;   // pos_ mirrors each interpolator's own position, which starts at 1 after a reset
    bool primed_ = false;
    std::vector<juce::WindowedSincInterpolator> interpolators_;
    std::vector<std::vector<float>> pending_;
};

juce::Result writeConverted(juce::AudioFormatReader& reader, const juce::File& part, double targetRate,
                            const std::function<bool(double)>& keepGoing, const juce::String& sourceName) {
    const int channels = (int) reader.numChannels;
    const juce::int64 inFrames = reader.lengthInSamples;
    const bool resample = reader.sampleRate != targetRate;
    const juce::int64 outFrames = resample ? std::llround((double) inFrames * targetRate / reader.sampleRate) : inFrames;

    std::unique_ptr<juce::OutputStream> stream = part.createOutputStream();
    if (stream == nullptr) return juce::Result::fail("Couldn't write " + part.getFileName() + ".");
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                  .withSampleRate(targetRate)
                                                  .withNumChannels(channels)
                                                  .withBitsPerSample(24));
    if (writer == nullptr) return juce::Result::fail("Couldn't write " + part.getFileName() + ".");

    const auto couldntWrite = juce::Result::fail("Couldn't write " + part.getFileName() + ". Is the disk full?");
    juce::AudioBuffer<float> in(channels, kBlockFrames), out(channels, kBlockFrames);
    Resampler resampler(channels, reader.sampleRate / targetRate);
    juce::int64 readFrames = 0, written = 0;
    const auto drain = [&]() -> bool {
        while (written < outFrames) {
            const int made = resampler.pull(out, (int) std::min<juce::int64>(kBlockFrames, outFrames - written));
            if (made == 0) return true;
            if (! writer->writeFromAudioSampleBuffer(out, 0, made)) return false;
            written += made;
        }
        return true;
    };

    while (readFrames < inFrames) {
        const int frames = (int) std::min<juce::int64>(kBlockFrames, inFrames - readFrames);
        if (! reader.read(&in, 0, frames, readFrames, true, true))
            return juce::Result::fail(sourceName + " couldn't be read to the end.");
        readFrames += frames;
        if (resample) {
            resampler.push(in, frames);
            if (! drain()) return couldntWrite;
        } else {
            if (! writer->writeFromAudioSampleBuffer(in, 0, frames)) return couldntWrite;
            written += frames;
        }
        if (! keepGoing((double) readFrames / (double) inFrames)) return juce::Result::fail("The import was stopped.");
    }
    // The last outputs look ahead past the end of the input, into silence.
    while (written < outFrames) {
        resampler.pushSilence(256);
        if (! drain()) return couldntWrite;
    }
    writer.reset();   // writes the header and closes the file
    return juce::Result::ok();
}

}  // namespace

juce::String guessRoleId(const juce::String& fileStem, const BandDoc& band) {
    const auto words = wordsOf(fileStem);
    for (const auto& w : words)
        if (const auto* r = partRoleNamed(band, w)) return r->id;
    for (const auto& w : words)
        for (const auto& entry : kRoleWords)
            for (const auto* candidate : entry.words)
                if (w == candidate)
                    if (const auto* r = partRoleNamed(band, entry.role)) return r->id;
    return {};
}

bool looksLikeAClick(const juce::String& fileStem) {
    for (const auto& w : wordsOf(fileStem))
        if (w == "click" || w == "clk" || w == "metronome" || w == "guide" || w == "cue") return true;
    return false;
}

ImportPlan inspectFiles(const juce::Array<juce::File>& dropped, const BandDoc& band) {
    ImportPlan plan;
    juce::Array<juce::File> candidates;
    for (const auto& f : dropped) {
        if (f.isDirectory()) {
            if (plan.title.isEmpty()) plan.title = f.getFileName();
            candidates.addArray(f.findChildFiles(juce::File::findFiles, false));
        } else if (f.existsAsFile()) {
            candidates.add(f);
        }
    }
    if (plan.title.isEmpty() && ! candidates.isEmpty()) plan.title = candidates[0].getParentDirectory().getFileName();
    std::sort(candidates.begin(), candidates.end(),
              [](const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural(b.getFileName()) < 0; });

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (const auto& f : candidates) {
        if (f.getFileName().startsWithChar('.')) continue;   // hidden files, such as the "._" copies macOS leaves
        if (isMidiFile(f)) {
            if (plan.midiFile == juce::File()) plan.midiFile = f;
            continue;
        }
        ImportFile item;
        item.file = f;
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(f));
        if (reader != nullptr && reader->numChannels > 0 && reader->lengthInSamples > 0 && reader->sampleRate > 0.0) {
            item.channels = (int) reader->numChannels;
            item.sampleRate = reader->sampleRate;
            item.seconds = (double) reader->lengthInSamples / reader->sampleRate;
        } else if (hasAudioExtension(f)) {
            item.problem = "This file couldn't be read as audio.";
            item.leaveOut = true;
        } else {
            continue;
        }
        plan.files.push_back(item);
    }

    juce::StringArray stems;
    for (const auto& item : plan.files) stems.add(item.file.getFileNameWithoutExtension());
    const int lead = sharedLeadLength(stems);
    for (size_t i = 0; i < plan.files.size(); ++i) {
        auto& item = plan.files[i];
        const auto& stem = stems[(int) i];
        item.partName = stem.substring(lead).trim();
        if (item.partName.isEmpty()) item.partName = stem;
        if (item.problem.isNotEmpty()) continue;
        if (looksLikeAClick(stem)) item.leaveOut = true;
        else item.roleId = guessRoleId(stem, band);
    }
    return plan;
}

juce::Result convertToBandWav(const juce::File& source, const juce::File& target, double targetRate,
                              const std::function<bool(double progress)>& keepGoing) {
    const auto sourceName = source.getFileName();
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));
    if (reader == nullptr) return juce::Result::fail(sourceName + " couldn't be read as audio.");
    if (reader->numChannels == 0 || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return juce::Result::fail(sourceName + " has no audio in it.");
    if (targetRate <= 0.0) return juce::Result::fail("The band's sample rate isn't set.");

    if (! target.getParentDirectory().createDirectory())
        return juce::Result::fail("Couldn't make the folder for " + target.getFileName() + ".");
    const auto part = target.getSiblingFile(target.getFileName() + ".part");
    part.deleteFile();

    auto result = writeConverted(*reader, part, targetRate, keepGoing, sourceName);
    if (result.wasOk() && ! part.moveFileTo(target))
        result = juce::Result::fail("Couldn't put " + target.getFileName() + " in place.");
    if (result.failed()) part.deleteFile();
    return result;
}

ImportResult importSong(const ImportPlan& plan, BandFolder& folder, CommandLayer& commands,
                        const std::function<bool(double progress)>& keepGoing) {
    std::vector<const ImportFile*> kept;
    double totalSeconds = 0.0;
    for (const auto& f : plan.files)
        if (! f.leaveOut && f.problem.isEmpty()) {
            kept.push_back(&f);
            totalSeconds += f.seconds;
        }
    if (kept.empty()) return {juce::Result::fail("There is nothing to import."), {}};

    SongDoc song;
    song.id = newId();
    song.title = plan.title.trim().isNotEmpty() ? plan.title.trim() : juce::String("Untitled");
    song.tempo = {TempoPointDoc{}};
    song.tempo[0].bpm = 120.0;

    const auto dir = folder.assignSongFolder(song.id, song.title);
    if (! dir.isDirectory()) return {juce::Result::fail("Couldn't make a folder for the song in the band folder."), {}};
    const auto giveUp = [&dir](juce::Result why) {
        dir.deleteRecursively();
        return ImportResult{why, {}};
    };
    const auto audioDir = dir.getChildFile("audio");
    if (! audioDir.createDirectory()) return giveUp(juce::Result::fail("Couldn't make the song's audio folder."));

    const double rate = commands.model().band().sampleRate;
    double secondsDone = 0.0;
    for (size_t i = 0; i < kept.size(); ++i) {
        const auto& f = *kept[i];
        // Each file's share of the progress is its length, or an equal share when no lengths are known.
        const double before = totalSeconds > 0.0 ? secondsDone / totalSeconds : (double) i / (double) kept.size();
        const double share = totalSeconds > 0.0 ? f.seconds / totalSeconds : 1.0 / (double) kept.size();
        const auto target = uniqueChild(audioDir, safeFileName(f.partName), ".wav");
        auto converted = convertToBandWav(f.file, target, rate, [&](double p) { return keepGoing(before + p * share); });
        if (converted.failed()) return giveUp(converted);
        secondsDone += f.seconds;

        PartDoc part;
        part.id = newId();
        part.name = f.partName;
        part.file = "audio/" + target.getFileName();
        part.roleId = f.roleId;
        part.channels = f.channels;
        song.parts.push_back(part);
        song.lengthSeconds = std::max(song.lengthSeconds, f.seconds);
    }

    auto* params = new juce::DynamicObject();
    juce::var holder(params);
    params->setProperty("song", toVar(song));
    auto added = commands.run("song.add", holder);
    if (! added.ok) return giveUp(juce::Result::fail(added.message));
    return {juce::Result::ok(), song.id};
}

}  // namespace sqlive::model
