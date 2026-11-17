#include "model/Sections.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include "model/SongAudio.h"

namespace sqlive::model {

namespace {

// A name as whole words: lower case, every character that isn't a letter (a digit, a hyphen, a space) between words.
juce::StringArray wordsOf(const juce::String& name) {
    const auto lower = name.toLowerCase();
    juce::String spaced;
    for (int i = 0; i < lower.length(); ++i)
        spaced += juce::CharacterFunctions::isLetter(lower[i]) ? lower[i] : (juce::juce_wchar) ' ';
    auto words = juce::StringArray::fromTokens(spaced, " ", "");
    words.removeEmptyStrings();
    return words;
}

// Whether `phrase` appears in `words`, word after word.
bool containsPhrase(const juce::StringArray& words, const juce::StringArray& phrase) {
    if (phrase.isEmpty()) return false;
    for (int at = 0; at + phrase.size() <= words.size(); ++at) {
        bool all = true;
        for (int i = 0; i < phrase.size() && all; ++i) all = words[at + i] == phrase[i];
        if (all) return true;
    }
    return false;
}

const SectionTypeDoc* typeNamed(const BandDoc& band, const char* name) {
    for (const auto& type : band.sectionTypes)
        if (type.name.equalsIgnoreCase(name)) return &type;
    return nullptr;
}

}  // namespace

std::vector<TiledSection> tileSections(const SongDoc& song, int songBars) {
    songBars = std::max(1, songBars);
    std::vector<const SectionDoc*> sorted;
    for (const auto& section : song.sections)
        if (section.startBar <= songBars) sorted.push_back(&section);
    std::stable_sort(sorted.begin(), sorted.end(), [](const SectionDoc* a, const SectionDoc* b) { return a->startBar < b->startBar; });
    std::vector<TiledSection> tiled;
    for (const auto* section : sorted) {
        TiledSection one{section->id, section->name, section->typeId, section->startBar, section->startBar + 1};
        if (!tiled.empty() && tiled.back().startBar == one.startBar) tiled.back() = one;   // the later in the list wins
        else tiled.push_back(one);
    }
    if (tiled.empty()) return {TiledSection{{}, "Song", {}, 1, songBars + 1}};
    tiled.front().startBar = 1;
    for (std::size_t i = 0; i + 1 < tiled.size(); ++i) tiled[i].endBar = tiled[i + 1].startBar;
    tiled.back().endBar = songBars + 1;
    return tiled;
}

std::vector<int> resolveArrangement(const SongDoc& song, const juce::String& arrangementId, const std::vector<TiledSection>& tiled) {
    std::vector<int> inOrder;
    for (int i = 0; i < (int) tiled.size(); ++i) inOrder.push_back(i);
    if (arrangementId.isEmpty()) return inOrder;
    const auto arrangement = std::find_if(song.arrangements.begin(), song.arrangements.end(),
                                          [&](const ArrangementDoc& a) { return a.id == arrangementId; });
    if (arrangement == song.arrangements.end()) return inOrder;
    std::vector<int> steps;
    for (const auto& step : arrangement->steps) {
        if (step.isEmpty()) continue;
        const auto found = std::find_if(tiled.begin(), tiled.end(), [&](const TiledSection& t) { return t.id == step; });
        if (found != tiled.end()) steps.push_back((int) (found - tiled.begin()));
    }
    return steps.empty() ? inOrder : steps;
}

juce::String guessSectionTypeId(const juce::String& sectionName, const BandDoc& band) {
    const auto words = wordsOf(sectionName);
    if (words.isEmpty()) return {};
    // Any of these words: the type of that name, if the band has it.
    const auto rule = [&](std::initializer_list<const char*> names, const char* typeName) -> juce::String {
        for (const auto* name : names)
            if (words.contains(name))
                if (const auto* type = typeNamed(band, typeName)) return type->id;
        return {};
    };
    for (const auto& type : band.sectionTypes)                   // a phrase of more than one word
        if (const auto phrase = wordsOf(type.name); phrase.size() > 1 && containsPhrase(words, phrase)) return type.id;
    if (auto id = rule({"pre"}, "Pre-chorus"); id.isNotEmpty()) return id;
    for (const auto& type : band.sectionTypes)                   // a type's own name
        if (const auto name = wordsOf(type.name); name.size() == 1 && words.contains(name[0])) return type.id;
    if (auto id = rule({"inst", "interlude"}, "Instrumental"); id.isNotEmpty()) return id;
    if (auto id = rule({"ending", "end", "out"}, "Outro"); id.isNotEmpty()) return id;
    if (auto id = rule({"refrain", "hook"}, "Chorus"); id.isNotEmpty()) return id;
    return rule({"break"}, "Breakdown");
}

int barsInSong(const SongDoc& song) {
    const auto tempo = tempoMapOf(song);
    const auto end = tempo.barBeatAt(songExtentSeconds(song, tempo).second);
    return std::max(1, end.beat > 1.0 ? end.bar : end.bar - 1);   // a bar begun counts as a whole one
}

double arrangementSeconds(const SongDoc& song, const juce::String& arrangementId) {
    const auto tempo = tempoMapOf(song);
    const double audioEnd = songExtentSeconds(song, tempo).second;
    const auto tiled = tileSections(song, barsInSong(song));
    double total = 0.0;
    for (const int i : resolveArrangement(song, arrangementId, tiled)) {
        const auto& section = tiled[(std::size_t) i];
        const double start = tempo.secondsAt({section.startBar, 1.0});
        const double end = i + 1 == (int) tiled.size() ? audioEnd : tempo.secondsAt({section.endBar, 1.0});
        total += std::max(0.0, end - start);
    }
    return std::round(total * 1000.0) / 1000.0;
}

engine::BarBeat announcePlace(const SongDoc& song, const TiledSection& section, CueLead lead) {
    const auto tempo = tempoMapOf(song);
    engine::BarBeat at{section.endBar, 1.0};
    switch (lead) {
    case CueLead::OneBar: at.bar -= 1; break;
    case CueLead::TwoBars: at.bar -= 2; break;
    case CueLead::TwoBeats:
        // Two beats back from the section's end, in each bar's own time signature.
        for (int left = 2; left > 0 && at.bar > 1;) {
            const int beats = tempo.beatsInBar(at.bar - 1);
            if (left < beats) { at = {at.bar - 1, 1.0 + beats - left}; left = 0; }
            else { at = {at.bar - 1, 1.0}; left -= beats; }
        }
        break;
    }
    if (at.bar < section.startBar) return {section.startBar, 1.0};
    return at;
}

}  // namespace sqlive::model
