// Session's cue voice and its imports of the band's own clips: the file work for Setup's Cue voice page and the Song
// editor's click sound, kept here so Session.cpp doesn't grow.

#include <atomic>
#include <thread>
#include <utility>

#include "app/Session.h"
#include "model/AtomicFile.h"
#include "model/FileNames.h"
#include "model/SongImport.h"

namespace sqlive::app {

// The voice being made. Its thread copies what it needs (the band folder's root, the band, the runner) and reaches the
// session only through MessageManager::callAsync with a weak pointer to this, checking `session` there, on the message
// thread: so nothing it posts ever reaches a session that has gone.
struct Session::VoiceJob {
    Session* session = nullptr;                                  // message thread; nullptr once the session is closing
    std::shared_ptr<std::atomic<bool>> stop = std::make_shared<std::atomic<bool>>(false);
    std::function<void(double)> onProgress;
    std::function<void(juce::Result)> onDone;
    juce::StringArray paths;                                     // the built-in clips it may make, inside the band folder
    std::thread thread;

    ~VoiceJob() {
        if (thread.joinable()) thread.join();
    }
};

namespace {

const juce::String kNotInShow = "Show mode never changes the band folder. Make the voice after the show.";

// The built-in clips VoiceMaker::makeBuiltIns makes for this band: each section type's that names the built-in clip of its
// own name, and the extra phrases'.
juce::StringArray builtInPaths(const model::BandDoc& band) {
    juce::StringArray paths;
    for (const auto& type : band.sectionTypes)
        if (const auto path = "cues/built-in/" + type.name + ".wav"; type.name.trim().isNotEmpty() && type.cueClip == path)
            paths.addIfNotAlreadyThere(path);
    for (const auto& phrase : VoiceMaker::extraPhrases()) paths.addIfNotAlreadyThere("cues/built-in/" + phrase + ".wav");
    return paths;
}

}  // namespace

juce::Result Session::makeVoice(bool evenIfPresent, std::function<void(double)> onProgress,
                                std::function<void(juce::Result)> onDone) {
    if (mode_ == Mode::Show) return juce::Result::fail(kNotInShow);
    if (voice_job_ != nullptr) return juce::Result::fail("The voice is already being made.");
    auto job = std::make_shared<VoiceJob>();
    job->session = this;
    job->onProgress = std::move(onProgress);
    job->onDone = std::move(onDone);
    job->paths = builtInPaths(model_.band());
    voice_job_ = job;

    job->thread = std::thread([root = folder_.root(), band = model_.band(), evenIfPresent, stop = job->stop,
                               weak = std::weak_ptr<VoiceJob>(job), runner = voice_runner_] {
        // Once stopped, the voice being said is killed (the runner watches the flag), and nothing more is put in place.
        VoiceMaker maker(runner, stop.get());
        const model::BandFolder folder(root);
        auto result = maker.makeBuiltIns(folder, band, evenIfPresent, [&](double progress) {
            if (stop->load()) return;
            juce::MessageManager::callAsync([weak, progress] {
                if (const auto job = weak.lock(); job != nullptr && job->session != nullptr && job->onProgress)
                    job->onProgress(progress);
            });
        });
        if (stop->load()) result = juce::Result::fail(kNotInShow);
        juce::MessageManager::callAsync([weak, result] {
            if (const auto job = weak.lock(); job != nullptr && job->session != nullptr) job->session->finishVoice(job, result);
        });
    });
    return juce::Result::ok();
}

bool Session::makingVoice() const { return voice_job_ != nullptr; }

void Session::setVoiceRunner(VoiceMaker::Runner runner) { voice_runner_ = std::move(runner); }

void Session::finishVoice(const std::shared_ptr<VoiceJob>& job, juce::Result result) {
    if (job != voice_job_) return;
    if (job->thread.joinable()) job->thread.join();              // this was the last thing it posted: it has ended, or is ending
    auto onDone = std::move(job->onDone);
    voice_job_ = nullptr;
    for (const auto& path : job->paths) cues_.forget(path);      // read again when next asked for: the songs say the new ones
    refresh();
    if (onDone) onDone(result);
}

void Session::stopVoice(bool closing) {
    if (voice_job_ == nullptr) return;
    voice_job_->stop->store(true);
    if (closing) voice_job_->session = nullptr;                  // nothing it posted reaches a session that is going
    // The voice being said is killed at once, so this is a moment, not a clip's 20 s; once it returns, nothing more is
    // written into the band folder.
    if (voice_job_->thread.joinable()) voice_job_->thread.join();
    if (closing) voice_job_ = nullptr;                           // else finishVoice, already posted, calls onDone
}

// ---- Importing the band's own clips ------------------------------------------------------------------------------------

juce::String Session::importCueClip(const juce::File& source) { return importAudio(source, "cues"); }
juce::String Session::importClickSound(const juce::File& source) { return importAudio(source, "clicks"); }

juce::String Session::importAudio(const juce::File& source, const juce::String& folderName) {
    if (mode_ == Mode::Show || !source.existsAsFile()) return {};
    // Converted outside the band folder; the free name is chosen only once it is whole, just before it is written in.
    const auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("Smart Q Live import", ".wav", false);
    const auto converted = model::convertToBandWav(source, temp, model_.band().sampleRate, [](double) { return true; });
    juce::MemoryBlock bytes;
    const bool read = converted.wasOk() && temp.loadFileAsData(bytes) && bytes.getSize() > 0;
    temp.deleteFile();
    if (!read) return {};
    const auto folder = folder_.root().getChildFile(folderName);
    const auto target = model::uniqueChild(folder, model::safeFileName(source.getFileNameWithoutExtension()), ".wav");
    const auto path = folderName + "/" + target.getFileName();
    if (!model::isPathInsideFolder(path) || model::writeFileAtomically(target, bytes).failed()) return {};
    cues_.forget(path);                                          // a clip of that name looked for before, and missing then
    changed();
    return path;
}

}  // namespace sqlive::app
