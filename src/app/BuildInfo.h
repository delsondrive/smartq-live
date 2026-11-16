#pragma once

#include <juce_core/juce_core.h>

namespace sqlive {

// Which build this is: the version the project declares and the commit it was made from. juce_core only, so the
// plug-in and the bridge each carry their own, and the check that they match is one they can both make.
struct BuildInfo {
    juce::String version;           // "0.1.0", the CMake project version
    juce::String commit;            // the short commit, or "unknown" when the build had no git
    bool modified = false;          // the build had uncommitted changes
    juce::int64 commitSeconds = 0;  // the commit's time, seconds since 1970 UTC; 0 when unknown
};

// This build's, defined in the BuildInfo.cpp that CMake writes at configure time.
const BuildInfo& buildInfo();

// The commit, "c279c4a", with a + after it when the build had uncommitted changes. "unknown" when there was no git.
juce::String buildStamp(const BuildInfo&);

// "Smart Q Live 0.1.0 · build c279c4a · 2026-11-09 02:06", the time in local time. Without the time when it is unknown.
juce::String buildLine(const BuildInfo&);

}  // namespace sqlive
