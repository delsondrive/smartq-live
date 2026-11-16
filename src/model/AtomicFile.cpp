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
    tmp.deleteFile();
    {
        juce::FileOutputStream out(tmp);
        if (out.failedToOpen()) return fail("the temporary file couldn't be created (" + out.getStatus().getErrorMessage() + ").");
        if (!out.write(text.toRawUTF8(), text.getNumBytesAsUTF8()) || (out.flush(), out.getStatus().failed())) {
            auto why = out.getStatus().getErrorMessage();
            tmp.deleteFile();
            return fail("it couldn't be written (" + (why.isEmpty() ? juce::String("the disk may be full") : why) + ").");
        }
    }  // closed before the swap

    const bool swapped = target.exists() ? tmp.replaceFileIn(target) : tmp.moveFileTo(target);
    if (!swapped) {
        tmp.deleteFile();
        return fail("the new version couldn't replace the old one. Is the file open in another program?");
    }
    return juce::Result::ok();
}

}  // namespace sqlive::model
