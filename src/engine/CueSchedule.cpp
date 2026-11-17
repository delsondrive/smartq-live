#include "engine/CueSchedule.h"

#include <algorithm>

namespace sqlive::engine {
namespace {

const std::shared_ptr<const PartAudio> kNoClip;   // what clipOf gives for a section with none

} // namespace

void CueSchedule::reset() noexcept {
    song_ = nullptr;
    at_ = kNever;
    passed_ = false;
    early_ = false;
    said_ = -1;
    next_ = 0;
}

void CueSchedule::enterStep(const PreparedSong& song, const Arrangement& arrangement, SamplePos songTime) noexcept {
    if (song_ != &song) next_ = 0;   // another song's cursor means nothing here; fire finds its place again
    song_ = &song;
    const int section = sectionIndex(arrangement.step());
    at_ = section >= 0 && static_cast<std::size_t>(section) < song.announceAt.size()
              ? song.announceAt[static_cast<std::size_t>(section)] : kNever;
    passed_ = songTime > at_;
    early_ = false;
    said_ = passed_ ? toAnnounce(arrangement) : -1;
}

// The manual cues at songTime (the cursor found again by bisection when song time moved other than straight on, as a jump
// does: those jumped over are skipped), then the announcement, if this is its moment and no manual cue was said at it.
void CueSchedule::fire(const Arrangement& arrangement, SamplePos songTime, CuePlayer& player) noexcept {
    if (song_ == nullptr) return;
    const auto& cues = song_->cues;
    const std::size_t count = cues.size();
    if (next_ > count || (next_ > 0 && cues[next_ - 1].at >= songTime) || (next_ < count && cues[next_].at < songTime)) {
        const auto it = std::lower_bound(cues.begin(), cues.end(), songTime,
                                         [](const CueEvent& c, SamplePos t) { return c.at < t; });
        next_ = static_cast<std::size_t>(it - cues.begin());
    }
    bool manual = false;
    for (; next_ < count && cues[next_].at == songTime; ++next_) {
        if (!cues[next_].clip) continue;
        player.say(cues[next_].clip);
        manual = true;
    }
    if (passed_ || songTime != at_) return;
    passed_ = true;
    early_ = false;
    const int wanted = toAnnounce(arrangement);
    if (manual) said_ = wanted;              // the band's own cue: this boundary's announcement is skipped
    else sayInstead(wanted, player);         // said_ is still -1, unless a NextBar choice was said already
}

std::int64_t CueSchedule::until(SamplePos songTime) const noexcept {
    if (song_ == nullptr) return kNever;
    SamplePos next = kNever;
    if (!passed_ && at_ > songTime) next = at_;
    const auto& cues = song_->cues;
    if (next_ < cues.size()) next = std::min(next, cues[next_].at);
    if (next == kNever) return kNever;
    return std::max<std::int64_t>(1, next - songTime);   // cues out of order could point behind songTime
}

void CueSchedule::changed(const Arrangement& arrangement, bool choiceAtOnce, CuePlayer& player) noexcept {
    if (song_ == nullptr) return;
    if (!passed_) {
        if (choiceAtOnce) {
            early_ = true;
        } else if (early_ && arrangement.pending() < 0) {   // the early choice is gone: wait for the moment again
            early_ = false;
            sayInstead(-1, player);
            return;
        }
        if (!early_) return;
    }
    sayInstead(toAnnounce(arrangement), player);
}

int CueSchedule::sectionIndex(int step) const noexcept {
    const PreparedSong& song = *song_;
    if (step < 0 || song.sections.empty() || static_cast<std::size_t>(step) >= song.arrangement.size()) return -1;
    const std::uint16_t index = song.arrangement[static_cast<std::size_t>(step)];
    return index < song.sections.size() ? static_cast<int>(index) : -1;
}

int CueSchedule::toAnnounce(const Arrangement& arrangement) const noexcept {
    if (arrangement.looping()) return -1;   // nothing new will play
    const int next = arrangement.nextStep();
    return next >= 0 ? sectionIndex(next) : -1;
}

const std::shared_ptr<const PartAudio>& CueSchedule::clipOf(int section) const noexcept {
    const auto& clips = song_->sectionCue;
    return section >= 0 && static_cast<std::size_t>(section) < clips.size() ? clips[static_cast<std::size_t>(section)] : kNoClip;
}

void CueSchedule::sayInstead(int section, CuePlayer& player) noexcept {
    if (section == said_) return;
    const PartAudio* before = clipOf(said_).get();
    said_ = section;
    if (const auto& clip = clipOf(section)) player.say(clip);
    else if (before != nullptr && player.speaking() == before) player.silence();
}

} // namespace sqlive::engine
