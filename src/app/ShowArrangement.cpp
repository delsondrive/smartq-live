#include "app/ShowArrangement.h"

#include <algorithm>

#include "engine/Arrangement.h"
#include "model/Sections.h"

namespace sqlive::app {

namespace {

const juce::String kThen = juce::String::fromUTF8("then \xE2\x86\x92 ");   // "then → "

engine::Arrangement probeOf(const engine::PreparedSong& song) {
    engine::Arrangement probe;
    probe.reset(&song);
    return probe;
}

// How many bar starts the song has in [from, to).
int barStartsIn(const engine::PreparedSong& song, engine::SamplePos from, engine::SamplePos to) {
    const auto& bars = song.barStarts;
    const auto first = std::lower_bound(bars.begin(), bars.end(), from);
    const auto last = std::lower_bound(bars.begin(), bars.end(), to);
    return static_cast<int>(std::max<std::ptrdiff_t>(0, last - first));
}

engine::SamplePos lengthOf(const engine::Arrangement& probe, int step) {
    const auto& section = probe.sectionOf(step);
    return std::max<engine::SamplePos>(0, section.end - section.start);
}

}  // namespace

// ---- The steps ---------------------------------------------------------------------------------------------------------

std::vector<StepView> ShowArrangement::stepsOf(const model::SongDoc& doc, const model::BandDoc& band,
                                               const engine::PreparedSong& song) {
    // The prepared sections start where tiled sections do (buildPreparedSong drops those past the audio and folds a short
    // last one into the one before): each is named after the last tiled section starting at or before it.
    const auto tiled = model::tileSections(doc, model::barsInSong(doc));
    std::vector<engine::SamplePos> starts;
    for (const auto& t : tiled) starts.push_back(song.tempo.sampleAt({t.startBar, 1.0}, song.sampleRate));
    const auto probe = probeOf(song);
    std::vector<StepView> steps;
    for (int step = 0; step < probe.stepCount(); ++step) {
        const auto& section = probe.sectionOf(step);
        StepView view;
        view.colour = juce::Colours::transparentBlack;
        for (std::size_t i = 0; i < tiled.size(); ++i)
            if (starts[i] <= section.start) {
                view.name = tiled[i].name;
                view.typeId = tiled[i].typeId;
            }
        for (const auto& type : band.sectionTypes)
            if (view.typeId.isNotEmpty() && type.id == view.typeId) view.colour = type.colour;
        view.bars = std::max(1, barStartsIn(song, section.start, section.end));
        steps.push_back(std::move(view));
    }
    return steps;
}

int ShowArrangement::startStep(Action a) {
    const int index = static_cast<int>(a), first = static_cast<int>(Action::Section1);
    return index >= first && index <= static_cast<int>(Action::Section10) ? index - first : -1;
}

engine::SamplePos ShowArrangement::countOffStart(const engine::PreparedSong& song, int step) {
    // As Engine::startAtStep plays it: one bar, at the tempo of the bar the step starts in.
    const auto stepStart = probeOf(song).sectionOf(step).start;
    const auto& bars = song.barStarts;
    const int bar = std::max(1, static_cast<int>(std::upper_bound(bars.begin(), bars.end(), stepStart) - bars.begin()));
    const auto oneBar = song.tempo.sampleAt({bar + 1, 1.0}, song.sampleRate) - song.tempo.sampleAt({bar, 1.0}, song.sampleRate);
    return stepStart - std::max<engine::SamplePos>(0, oneBar);
}

// ---- Following the song ------------------------------------------------------------------------------------------------

void ShowArrangement::started(std::uint64_t serial, int leadStep, engine::SamplePos from) {
    serial_ = serial;
    lead_step_ = std::max(0, leadStep);
    from_ = from;
    first_step_ = -1;
    entries_ = 0;
    first_entered_at_ = 0;
    asked_ = {};
}

void ShowArrangement::stepEntered(std::uint64_t serial, int step, std::int64_t streamTime) {
    if (serial == 0 || serial < serial_ || step < 0) return;    // a song before the one followed: it no longer steps
    if (serial != serial_) started(serial, 0, 0);               // one that followed on, from its start
    if (entries_ == 0) {
        first_step_ = step;
        first_entered_at_ = streamTime;
    }
    ++entries_;
}

int ShowArrangement::playingStep(const ArrangementNow& p) const {
    if (p.status.playingSerial == p.serial && p.status.step >= 0) return p.status.step;
    if (!follows(p.serial) || entries_ == 0) return follows(p.serial) ? lead_step_ : 0;   // the lead-in
    return -1;                                                  // after the last step
}

double ShowArrangement::firstOffset(const engine::PreparedSong& song) const {
    // Started inside the first step's section (Edit mode's playSong): the arrangement is counted from the section's start.
    const auto section = probeOf(song).sectionOf(first_step_);
    return from_ > section.start && from_ < section.end ? static_cast<double>(from_ - section.start) : 0.0;
}

ShowArrangement::Choice ShowArrangement::choiceOf(const ArrangementNow& p) const {
    if (p.status.playingSerial != p.serial || p.song == nullptr) return {};
    const Choice shown{p.status.pendingStep, p.status.looping};
    // What was asked last, until the engine shows it. Once it does, or the playing step has handed over since (which uses
    // a choice up), or a second has gone by, the engine's own word is taken again.
    const bool stale = asked_.serial != p.serial || asked_.step != p.status.step || shown == asked_.choice
                       || p.status.streamTime > asked_.streamTime + static_cast<std::int64_t>(p.song->sampleRate);
    if (stale) {
        asked_ = {};
        return shown;
    }
    return asked_.choice;
}

// ---- The section actions -----------------------------------------------------------------------------------------------

void ShowArrangement::perform(Action a, const ArrangementNow& p, engine::Engine& engine) {
    if (p.song == nullptr || p.status.playingSerial != p.serial) return;
    const int count = probeOf(*p.song).stepCount();
    const int current = playingStep(p);
    if (current < 0 || count == 0) return;
    const auto choice = choiceOf(p);
    Choice asked = choice;
    switch (a) {
    case Action::NextSectionLater:
    case Action::NextSectionEarlier: {
        const bool later = a == Action::NextSectionLater;
        const int from = choice.pending >= 0 ? choice.pending : current + 1;
        if (later && from >= count - 1 && choice.pending < 0) return;   // nothing comes after the last step
        const int to = std::clamp(from + (later ? 1 : -1), 0, count - 1);
        asked = {to == current + 1 ? -1 : to, false};           // the step after the playing one comes anyway
        engine.chooseNextStep(p.serial, asked.pending);
        break;
    }
    case Action::LoopSection:
        asked = {choice.looping ? choice.pending : -1, !choice.looping};   // turning it on cancels a choice
        engine.setLoop(p.serial, asked.looping);
        break;
    case Action::CancelSection:
        asked = {};
        engine.chooseNextStep(p.serial, -1);
        break;
    default: {
        const int step = startStep(a);
        if (step < 0 || step >= count) return;
        asked = {step, false};
        engine.chooseNextStep(p.serial, step);
        break;
    }
    }
    asked_ = {p.serial, p.status.step, p.status.streamTime, asked};
}

// ---- What the views show -----------------------------------------------------------------------------------------------

ArrangementView ShowArrangement::idle(const std::vector<StepView>& steps) {
    ArrangementView view;
    view.steps = steps;
    return view;
}

ArrangementView ShowArrangement::view(const ArrangementNow& p) const {
    auto view = idle(p.steps != nullptr ? *p.steps : std::vector<StepView>());
    const bool live = p.status.playingSerial == p.serial;
    const int count = static_cast<int>(view.steps.size());
    const auto choice = choiceOf(p);
    const int current = live && p.status.step < count ? p.status.step : -1;
    const int pending = choice.pending < count ? choice.pending : -1;
    view.current = current;
    view.looping = choice.looping;
    view.pending = pending >= 0 && pending != current + 1 ? pending : -1;
    if (current >= 0 && p.song != nullptr) {
        const auto section = probeOf(*p.song).sectionOf(current);
        // The bar playing (the last bar start at or before the song time) and the ones after it, to the section's end.
        if (p.songTime < section.end) view.barsLeft = barStartsIn(*p.song, p.songTime + 1, section.end) + 1;
    }
    int next = -1;
    if (current >= 0) next = view.looping ? current : pending >= 0 ? pending : current + 1;
    else if (!follows(p.serial) || entries_ == 0) next = playingStep(p);   // the lead-in: the step it leads into
    if (next >= count) next = -1;
    view.then = kThen + (next < 0 ? juce::String("the end")
                                  : view.steps[static_cast<std::size_t>(next)].name + (next == current ? " again" : ""));
    return view;
}

double ShowArrangement::arrangedSeconds(const ArrangementNow& p) const {
    if (p.song == nullptr || p.song->sampleRate <= 0.0) return 0.0;
    const auto& song = *p.song;
    // Once a step has been entered: the samples played since the first one began, by the status's stream time. Nothing
    // here depends on which step the events have told of so far, so a jump the events haven't reported yet (or one they
    // reported before the status shows it) moves nothing.
    if (follows(p.serial) && entries_ > 0 && p.status.playingSerial == p.serial)
        return (firstOffset(song) + static_cast<double>(p.status.streamTime - first_entered_at_)) / song.sampleRate;
    // The lead-in, or a status from before the first step's event: from the start of the step it leads into.
    return static_cast<double>(p.songTime - probeOf(song).sectionOf(std::max(0, playingStep(p))).start) / song.sampleRate;
}

double ShowArrangement::arrangedLengthSeconds(const ArrangementNow& p) const {
    if (p.song == nullptr || p.song->sampleRate <= 0.0) return 0.0;
    const auto& song = *p.song;
    const auto probe = probeOf(song);
    const int count = probe.stepCount();
    const int pending = choiceOf(p).pending;
    const auto stepsFrom = [&](int first) {
        engine::SamplePos total = 0;
        for (int step = std::max(0, first); step < count; ++step) total += lengthOf(probe, step);
        return total;
    };
    const int current = playingStep(p);
    if (current < 0) return arrangedSeconds(p);                 // the last step is over: nothing more comes
    const auto then = stepsFrom(pending >= 0 ? pending : current + 1);
    if (p.status.playingSerial != p.serial || p.status.step < 0)   // the lead-in: its step, then what follows it
        return static_cast<double>(lengthOf(probe, current) + then) / song.sampleRate;
    // The step the status names, with the song time of the same status.
    const auto rest = std::max<engine::SamplePos>(0, probe.sectionOf(current).end - p.songTime);
    return arrangedSeconds(p) + static_cast<double>(rest + then) / song.sampleRate;
}

}  // namespace sqlive::app
