#include "app/BuildInfo.h"

namespace sqlive {

juce::String buildStamp(const BuildInfo& info) {
    if (info.commit == "unknown") return info.commit;
    return info.modified ? info.commit + "+" : info.commit;
}

juce::String buildLine(const BuildInfo& info) {
    const juce::String dot = juce::String(juce::CharPointer_UTF8(" \xc2\xb7 "));
    juce::String line = "Smart Q Live " + info.version + dot + "build " + buildStamp(info);
    if (info.commitSeconds > 0) line += dot + juce::Time(info.commitSeconds * 1000).formatted("%Y-%m-%d %H:%M");
    return line;
}

}  // namespace sqlive
