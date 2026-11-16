#pragma once

#include <juce_core/juce_core.h>

namespace sqlive::test {

// A fresh folder under the system temp folder, deleted with everything in it when this goes out of scope.
class TempDir {
public:
    TempDir()
        : folder_(juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("sqlive-test-" + juce::Uuid().toDashedString())) {
        folder_.createDirectory();
    }
    ~TempDir() { folder_.deleteRecursively(); }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const juce::File& dir() const { return folder_; }

private:
    juce::File folder_;
};

}  // namespace sqlive::test
