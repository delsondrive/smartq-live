#include "model/AtomicFile.h"

namespace sqlive::model {

namespace {

// A move or a swap refused while another program holds a file for a moment: tried again after these waits, about half a
// second in all, while what is to be moved is still there.
template <typename Attempt>
bool retrying(const juce::File& from, Attempt&& attempt) {
    static constexpr int kWaitsMs[] = {5, 10, 20, 40, 80, 150, 200};
    for (const int waitMs : kWaitsMs) {
        if (attempt()) return true;
        if (!from.exists()) return false;                          // nothing left to move
        juce::Thread::sleep(waitMs);
    }
    return attempt();
}

// Puts the written temp file in the target's place. A swap refused for a moment (ReplaceFile: "unable to remove the file to
// be replaced"; MoveFile: a sharing violation) leaves both files as they were, so it can be tried again. Each try looks
// again at whether the target is there.
bool swapIn(const juce::File& tmp, const juce::File& target) {
    return retrying(tmp, [&] { return target.exists() ? tmp.replaceFileIn(target) : tmp.moveFileTo(target); });
}

}  // namespace

bool moveRetrying(const juce::File& from, const juce::File& to) {
    return retrying(from, [&] { return from.moveFileTo(to); });
}

juce::Result writeFileAtomically(const juce::File& target, const juce::String& text) {
    return writeFileAtomically(target, juce::MemoryBlock(text.toRawUTF8(), text.getNumBytesAsUTF8()));
}

juce::Result writeFileAtomically(const juce::File& target, const juce::MemoryBlock& bytes) {
    const auto name = target.getFileName();
    auto fail = [&](const juce::String& why) { return juce::Result::fail("Couldn't save " + name + ": " + why); };

    if (target.isDirectory()) return fail("there is a folder with that name.");

    const auto parent = target.getParentDirectory();
    if (!parent.isDirectory()) {
        auto made = parent.createDirectory();
        if (made.failed()) return fail("the folder " + parent.getFullPathName() + " couldn't be created (" + made.getErrorMessage() + ").");
    }

    const juce::File tmp(target.getFullPathName() + ".tmp");
    // A stale temp file must go: the stream appends, and old text would end up inside the saved file.
    if (tmp.exists() && !tmp.deleteFile())
        return fail("an old temporary file (" + tmp.getFileName() + ") is in the way and couldn't be removed.");

    juce::String writeProblem;
    {
        juce::FileOutputStream out(tmp);
        if (out.failedToOpen()) return fail("the temporary file couldn't be created (" + out.getStatus().getErrorMessage() + ").");
        // Nothing to write is still a write: an empty file (and the stream won't take a null pointer).
        const bool written = bytes.getSize() == 0 || out.write(bytes.getData(), bytes.getSize());
        if (!written || (out.flush(), out.getStatus().failed())) {
            auto why = out.getStatus().getErrorMessage();
            writeProblem = "it couldn't be written (" + (why.isEmpty() ? juce::String("the disk may be full") : why) + ").";
        }
    }  // closed here: Windows won't delete a file that is still open, and the swap needs it closed too
    if (writeProblem.isNotEmpty()) {
        tmp.deleteFile();
        return fail(writeProblem);
    }
    if (!swapIn(tmp, target)) {
        tmp.deleteFile();
        return fail("the new version couldn't replace the old one. Is the file open in another program?");
    }
    return juce::Result::ok();
}

}  // namespace sqlive::model
