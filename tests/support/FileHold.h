#pragma once

#include <juce_core/juce_core.h>

namespace sqlive::test {

// Holds a file open with no sharing at all, so that replacing or deleting it is refused, until this goes out of scope.
// held() is false where that can't be arranged (the platform twin FileHold_posix.cpp); a test using it should then skip.
class ExclusiveFileHold {
public:
    explicit ExclusiveFileHold(const juce::File& file);
    ~ExclusiveFileHold();
    ExclusiveFileHold(const ExclusiveFileHold&) = delete;
    ExclusiveFileHold& operator=(const ExclusiveFileHold&) = delete;

    bool held() const { return handle_ != nullptr; }

private:
    void* handle_ = nullptr;
};

}  // namespace sqlive::test
