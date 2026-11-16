#include "model/AtomicFile.h"

namespace sqlive::model {

juce::Result writeFileAtomically(const juce::File& target, const juce::String& text) {
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
    if (tmp.exists() && !tmp.deleteFile()) return fail("an old temporary file (" + tmp.getFileName() + ") is in the way and couldn't be removed.");

    juce::String writeProblem;
    {
        juce::FileOutputStream out(tmp);
        if (out.failedToOpen()) return fail("the temporary file couldn't be created (" + out.getStatus().getErrorMessage() + ").");
        if (!out.write(text.toRawUTF8(), text.getNumBytesAsUTF8()) || (out.flush(), out.getStatus().failed())) {
            auto why = out.getStatus().getErrorMessage();
            writeProblem = "it couldn't be written (" + (why.isEmpty() ? juce::String("the disk may be full") : why) + ").";
        }
    }  // closed here: Windows won't delete a file that is still open, and the swap needs it closed too
    if (writeProblem.isNotEmpty()) {
        tmp.deleteFile();
        return fail(writeProblem);
    }
    const bool swapped = target.exists() ? tmp.replaceFileIn(target) : tmp.moveFileTo(target);
    if (!swapped) {
        tmp.deleteFile();
        return fail("the new version couldn't replace the old one. Is the file open in another program?");
    }
    return juce::Result::ok();
}

}  // namespace sqlive::model
