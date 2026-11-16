#include "model/FileNames.h"

namespace sqlive::model {

namespace {
bool isReservedWindowsName(const juce::String& stem) {
    static const char* const names[] = {"CON", "PRN", "AUX", "NUL"};
    for (auto* n : names)
        if (stem.equalsIgnoreCase(n)) return true;
    if (stem.length() == 4 && (stem.substring(0, 3).equalsIgnoreCase("COM") || stem.substring(0, 3).equalsIgnoreCase("LPT")))
        return stem[3] >= '1' && stem[3] <= '9';
    return false;
}
juce::String trimSpacesAndDots(const juce::String& s) {
    return s.trimCharactersAtStart(" .").trimCharactersAtEnd(" .");
}
}  // namespace

juce::String safeFileName(const juce::String& title) {
    juce::String out;
    for (auto c : title) {
        if (c < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == 0x5C || c == '|' || c == '?' || c == '*') out << '-';
        else out << juce::String::charToString(c);
    }
    out = trimSpacesAndDots(out);
    if (out.isEmpty()) return "Untitled";
    out = trimSpacesAndDots(out.substring(0, 60));
    if (out.isEmpty()) return "Untitled";

    // "CON" and "CON.txt" are both reserved: the stem is what comes before the first dot.
    const int dot = out.indexOfChar('.');
    const auto stem = dot < 0 ? out : out.substring(0, dot);
    if (isReservedWindowsName(stem)) out = trimSpacesAndDots((stem + "_" + (dot < 0 ? juce::String() : out.substring(dot))).substring(0, 60));
    return out;
}

bool isPathInsideFolder(const juce::String& path) {
    if (path.isEmpty() || path.startsWithChar('/') || path.startsWithChar('\\') || path.containsChar(':')) return false;
    juce::StringArray steps;
    steps.addTokens(path, "/\\", "");
    return ! steps.contains("..");
}

juce::File uniqueChild(const juce::File& folder, const juce::String& name, const juce::String& extension) {
    auto candidate = folder.getChildFile(name + extension);
    for (int n = 2; candidate.exists(); ++n) candidate = folder.getChildFile(name + " " + juce::String(n) + extension);
    return candidate;
}

}  // namespace sqlive::model
