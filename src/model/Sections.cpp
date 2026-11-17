#include "model/Sections.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include "engine/PreparedSong.h"
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

Announcements announcements(const SongDoc& song, const BandDoc& band, const juce::String& arrangementId) {
    const double rate = band.sampleRate > 0.0 ? band.sampleRate : 48000.0;
    const auto end = songExtentSeconds(song, tempoMapOf(song)).second;
    return announcements(song, band, arrangementId, (engine::SamplePos) std::llround(end * rate), rate);
}

Announcements announcements(const SongDoc& song, const BandDoc& band, const juce::String& arrangementId, engine::SamplePos end,
                            double sampleRate) {
    Announcements a;
    const auto tempo = tempoMapOf(song);
    const auto bar = [&](int number) { return tempo.sampleAt({number, 1.0}, sampleRate); };
    auto tiled = tileSections(song, barsInSong(song));
    // Those starting where the audio has ended would play nothing. The first starts at bar 1, so it goes only when there is
    // no audio at all.
    std::erase_if(tiled, [&](const TiledSection& t) { return bar(t.startBar) >= end; });
    if (tiled.empty()) return a;
    // The last one left ends where the audio does; its cue is placed from the first bar line at or after that.
    auto& last = tiled.back();
    last.endBar = last.startBar + 1;
    while (bar(last.endBar) < end) ++last.endBar;
    auto steps = resolveArrangement(song, arrangementId, tiled);
    // A last section shorter than a jump's crossfade would be cut short by a hand-over still fading into it: its few
    // samples end the section before it instead. The steps that named it are dropped, so the sections in order still play
    // once each; only an arrangement that named nothing else plays the section before in their place (no step at all
    // would mean the sections in order).
    const int lastIndex = (int) tiled.size() - 1;
    if (lastIndex > 0 && end - bar(tiled.back().startBar) < engine::jumpFadeLength(sampleRate)) {
        tiled[(std::size_t) lastIndex - 1].endBar = tiled.back().endBar;
        tiled.pop_back();
        const bool onlyThat = std::all_of(steps.begin(), steps.end(), [&](int step) { return step == lastIndex; });
        if (onlyThat) std::replace(steps.begin(), steps.end(), lastIndex, lastIndex - 1);
        else std::erase(steps, lastIndex);
    }

    for (const auto& t : tiled) {
        const auto type = std::find_if(band.sectionTypes.begin(), band.sectionTypes.end(),
                                       [&](const SectionTypeDoc& s) { return t.typeId.isNotEmpty() && s.id == t.typeId; });
        a.places.push_back(announcePlace(song, t, band.cueLead));
        a.clips.push_back(type == band.sectionTypes.end() ? juce::String() : type->cueClip);
    }
    a.sections = std::move(tiled);
    for (const int step : steps)
        if (step >= 0 && (std::size_t) step < a.sections.size()) a.steps.push_back(step);
    for (std::size_t k = 0; k + 1 < a.steps.size(); ++k) {
        const int here = a.steps[k], next = a.steps[k + 1];
        const auto& clip = a.clips[(std::size_t) next];
        if (clip.isEmpty()) continue;
        a.said.push_back({(int) k, here, a.places[(std::size_t) here], next, a.sections[(std::size_t) next].typeId, clip});
    }
    return a;
}

}  // namespace sqlive::model
