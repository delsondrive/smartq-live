#include "tests/support/FileHold.h"

namespace sqlive::test {

// POSIX file systems let a held file be replaced, so there is nothing to hold: held() stays false.
ExclusiveFileHold::ExclusiveFileHold(const juce::File&) {}
ExclusiveFileHold::~ExclusiveFileHold() = default;

}  // namespace sqlive::test
