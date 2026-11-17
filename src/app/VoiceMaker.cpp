#include "app/VoiceMaker.h"

#include <set>
#include <utility>
#include <vector>

#include "model/AtomicFile.h"

namespace sqlive::app {

namespace {

constexpr int kMaxCharacters = 80;
constexpr int kTimeoutMs = 20000;

// Only letters, digits, spaces and , . ' - are kept, at most kMaxCharacters of them.
juce::String wordsOnly(const juce::String& text) {
    juce::String kept;
    for (auto p = text.getCharPointer(); !p.isEmpty();) {
        const auto c = p.getAndAdvance();
        if (juce::CharacterFunctions::isLetterOrDigit(c) || c == ' ' || c == ',' || c == '.' || c == '\'' || c == '-') kept += c;
    }
    return kept.trim().substring(0, kMaxCharacters).trim();
}

bool runAndWait(const VoiceCommand& command) {
    juce::StringArray args(command.program);
    args.addArray(command.arguments);
    juce::ChildProcess process;
    if (!process.start(args, 0)) return false;                     // 0: no output is read, so none can fill a pipe
    if (!process.waitForProcessToFinish(kTimeoutMs)) {
        process.kill();
        return false;
    }
    return process.getExitCode() == 0;
}

// Puts the finished voice in place through writeFileAtomically, as every write into the band folder goes: the target is
// the old clip or the new one, never part of it.
juce::Result placeVoice(const juce::File& voice, const juce::File& target) {
    juce::MemoryBlock bytes;
    if (!voice.loadFileAsData(bytes) || bytes.getSize() == 0)
        return juce::Result::fail("Couldn't save " + target.getFileName() + ": the voice that was made couldn't be read.");
    return model::writeFileAtomically(target, bytes);
}

}  // namespace

VoiceMaker::VoiceMaker(Runner runner) : runner_(runner ? std::move(runner) : Runner(runAndWait)) {}

juce::Result VoiceMaker::make(const juce::String& text, const juce::File& wavTarget) {
    const auto words = wordsOnly(text);
    if (words.isEmpty()) return juce::Result::fail("There are no words to say.");
    // In the system's temporary folder, outside the band folder: the clip is moved in only once it is whole.
    const auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("Smart Q Live voice", {}, false);
    if (temp.createDirectory().failed())
        return juce::Result::fail("The computer's voice couldn't be used: no temporary folder could be made.");
    const auto result = [&] {
        const auto command = voiceCommand(words, temp);
        if (!command.textFile.existsAsFile())
            return juce::Result::fail("The computer's voice couldn't be used: the words couldn't be written down.");
        if (!runner_(command) || !command.wavFile.existsAsFile() || command.wavFile.getSize() <= 0)
            return juce::Result::fail("The computer's voice couldn't say \"" + words + "\".");
        return placeVoice(command.wavFile, wavTarget);
    }();
    temp.deleteRecursively();
    return result;
}

juce::Result VoiceMaker::makeBuiltIns(const model::BandFolder& folder, const model::BandDoc& band, bool evenIfPresent,
                                      const std::function<void(double)>& progress) {
    struct Clip { juce::File file; juce::String words; };
    std::vector<Clip> clips;
    std::set<juce::String> seen;
    const auto& root = folder.root();
    const auto add = [&](const juce::String& path, const juce::String& words) {
        const auto file = root.getChildFile(path);
        if (file.isAChildOf(root) && seen.insert(file.getFullPathName()).second) clips.push_back({file, words});
    };
    for (const auto& type : band.sectionTypes)
        if (const auto path = "cues/built-in/" + type.name + ".wav"; type.name.trim().isNotEmpty() && type.cueClip == path)
            add(path, type.name);
    for (const auto& phrase : extraPhrases()) add("cues/built-in/" + phrase + ".wav", spokenAs(phrase));

    auto first = juce::Result::ok();
    for (std::size_t i = 0; i < clips.size(); ++i) {
        if (evenIfPresent || !clips[i].file.existsAsFile())
            if (const auto made = make(clips[i].words, clips[i].file); made.failed() && first.wasOk()) first = made;
        if (progress) progress((double) (i + 1) / (double) clips.size());
    }
    if (clips.empty() && progress) progress(1.0);
    return first;
}

juce::StringArray VoiceMaker::extraPhrases() { return {"Watch me", "Last time", "Two bars to stop", "1-2-3-4"}; }

juce::String VoiceMaker::spokenAs(const juce::String& phrase) {
    if (phrase == "1-2-3-4") return "One, two, three, four";
    return phrase;
}

}  // namespace sqlive::app
