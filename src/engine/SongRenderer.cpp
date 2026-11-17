#include "engine/SongRenderer.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace sqlive::engine {
namespace {

constexpr double kTimeConstantSeconds = 0.010;
constexpr double kSnap = 1e-6;

int clampRoles(int roleCount) noexcept { return std::clamp(roleCount, 0, kMaxRoles); }

} // namespace

void SongRenderer::prepare(double sampleRate, int maxBlockFrames) {
    coefficient_ = sampleRate > 0.0 ? 1.0 - std::exp(-1.0 / (kTimeConstantSeconds * sampleRate)) : 1.0;
    max_block_ = std::max(1, maxBlockFrames);
    click_.assign(static_cast<std::size_t>(max_block_), 0.0f);
    part_gain_.assign(kMaxParts, {});
    part_level_.assign(kMaxParts, 1.0f);
    part_mute_.assign(kMaxParts, 0);
    part_view_.assign(kMaxParts, {});
    part_auto_.assign(kMaxParts, {});
    cells_.assign(static_cast<std::size_t>(kMaxRoles) * kMaxBuses, {});
    click_gain_ = {};
    cue_gain_ = {1.0, 1.0};
    preview_gain_ = 1.0f;
    last_cue_level_ = 1.0f;
    song_ = nullptr;
    part_count_ = 0;
    solo_ = -1;
    count_in_first_ = count_in_end_ = 0;
    parts_moving_ = false;
    automated_ = 0;
    snap_pending_ = false;
    has_mix_ = false;
    bus_count_ = 0;
    role_rows_ = 0;
    click_role_ = -1;
    cues_role_ = -1;
    cells_moving_ = false;
}

void SongRenderer::step(Smoothed& s) const noexcept {
    s.value += coefficient_ * (s.target - s.value);
    if (std::abs(s.target - s.value) < kSnap) s.value = s.target;
}

void SongRenderer::setSong(const PreparedSong* song) noexcept {
    if (part_gain_.empty()) return;   // not prepared
    song_ = song;
    solo_ = -1;
    part_count_ = song != nullptr ? std::min(static_cast<int>(song->parts.size()), kMaxParts) : 0;
    for (int p = 0; p < part_count_; ++p) {
        const PreparedPart& part = song->parts[static_cast<std::size_t>(p)];
        part_level_[p] = part.gain;
        part_mute_[p] = part.mute ? 1 : 0;
        part_auto_[p] = {};
        part_auto_[p].points = part.automation;
        PartView& v = part_view_[p];
        v = {};
        if (!part.audio || part.role < 0 || part.role >= kMaxRoles) continue;
        const PartAudio& a = *part.audio;
        v.left = a.left.data();
        v.length = static_cast<SamplePos>(a.left.size());
        if (!a.right.empty()) {
            v.right = a.right.data();
            v.length = std::min(v.length, static_cast<SamplePos>(a.right.size()));
        }
        v.role = part.role;
    }
    updatePartTargets();
    for (int p = 0; p < part_count_; ++p) part_gain_[p].value = part_gain_[p].target;
    parts_moving_ = false;
    countAutomated();
    snap_pending_ = true;   // automated parts start at their level at the first sample, not gliding to it

    const float clickGain = song != nullptr ? song->click.gain : 1.0f;
    click_gain_ = {clickGain, clickGain};
    if (song != nullptr) {
        cue_gain_ = {song->cueGain, song->cueGain};
        last_cue_level_ = song->cueGain;
    } else {
        preview_gain_ = last_cue_level_;   // a voice fading out as the song ends carries on at the level it had
    }
    count_in_first_ = count_in_end_ = playable_first_ = 0;
    if (song != nullptr) {
        count_in_first_ = firstTickAt(std::min<SamplePos>(song->countInStart, 0));
        count_in_end_ = firstTickAt(0);
        playable_first_ = firstTickAt(song->start);
    }
}

std::size_t SongRenderer::firstTickAt(SamplePos at) const noexcept {
    const auto& ticks = song_->ticks;
    const auto it = std::lower_bound(ticks.begin(), ticks.end(), at, [](const ClickTick& t, SamplePos v) { return t.at < v; });
    return static_cast<std::size_t>(it - ticks.begin());
}

void SongRenderer::setMix(const MixSetup* mix) noexcept {
    if (cells_.empty()) return;   // not prepared
    if (mix == nullptr) {
        has_mix_ = false;
        bus_count_ = 0;
        click_role_ = -1;
        cues_role_ = -1;
        return;
    }
    const int busCount = std::min(static_cast<int>(mix->buses.size()), kMaxBuses);
    bool sameLayout = has_mix_ && busCount == bus_count_;
    for (int b = 0; sameLayout && b < busCount; ++b)
        sameLayout = mix->buses[b].firstChannel == bus_first_[b] && mix->buses[b].stereo == bus_stereo_[b];

    for (int b = 0; b < busCount; ++b) {
        bus_first_[b] = mix->buses[b].firstChannel;
        bus_stereo_[b] = mix->buses[b].stereo;
    }
    // The trim is folded into the cell, so a changed trim glides like a changed level.
    for (int r = 0; r < kMaxRoles; ++r) {
        for (int b = 0; b < kMaxBuses; ++b) {
            Smoothed& c = cells_[static_cast<std::size_t>(r) * kMaxBuses + b];
            c.target = b < busCount ? static_cast<double>(mix->at(r, b)) * static_cast<double>(mix->buses[b].trim) : 0.0;
            if (!sameLayout) c.value = c.target;
        }
    }
    // Rows past the new role count glide down to 0, so they stay in play until the next snap.
    role_rows_ = sameLayout ? std::max(role_rows_, clampRoles(mix->roleCount)) : clampRoles(mix->roleCount);
    cells_moving_ = sameLayout;
    bus_count_ = busCount;
    click_role_ = mix->clickRole;
    cues_role_ = mix->cuesRole;
    has_mix_ = true;
}

void SongRenderer::updatePartTargets() noexcept {
    for (int p = 0; p < part_count_; ++p) {
        part_gain_[p].target = partSilenced(p) ? 0.0 : static_cast<double>(part_level_[p]);
    }
    parts_moving_ = true;
}

void SongRenderer::setPartGain(int part, float linear) noexcept {
    if (part < 0 || part >= part_count_) return;
    part_level_[part] = linear;
    updatePartTargets();
}

void SongRenderer::setPartMute(int part, bool mute) noexcept {
    if (part < 0 || part >= part_count_) return;
    part_mute_[part] = mute ? 1 : 0;
    updatePartTargets();
}

void SongRenderer::setPartOverride(int part, bool overridden) noexcept {
    if (part < 0 || part >= part_count_) return;
    part_auto_[part].overridden = overridden;
    countAutomated();
    updatePartTargets();   // an overridden part glides to its fader alone; one that follows again, to its automation next sample
}

void SongRenderer::countAutomated() noexcept {
    automated_ = 0;
    for (int p = 0; p < part_count_; ++p)
        if (!part_auto_[p].points.empty() && !part_auto_[p].overridden) ++automated_;
}

// Sets the target of every part that follows automation to its fader (and mute, and solo) times its automation at `at`; with
// snap, each is put there at once. Called for each sample that advances, in order. A cursor that is not where `at` is
// expected finds its place by bisection (a jump, a start part way in), so no call but this one has to know about jumps.
void SongRenderer::followAutomation(SamplePos at, bool snap) noexcept {
    for (int p = 0; p < part_count_; ++p) {
        AutoCursor& c = part_auto_[p];
        if (c.points.empty() || c.overridden) continue;
        const auto n = static_cast<std::ptrdiff_t>(c.points.size());
        if (!c.placed || at != c.expected) {
            const auto after = std::upper_bound(c.points.begin(), c.points.end(), at,
                                                [](SamplePos t, const AutoPoint& point) { return t < point.at; });
            c.last = (after - c.points.begin()) - 1;
            c.placed = true;
            c.held = false;
        } else {
            while (c.last + 1 < n && c.points[static_cast<std::size_t>(c.last) + 1].at <= at) {
                ++c.last;
                c.held = false;
            }
        }
        c.expected = at + 1;
        // A segment whose two points are alike holds one gain: worked out when the segment is entered, not once a sample.
        float gain;
        if (automationFlat(c.points, c.last)) {
            if (!c.held) {
                c.heldGain = automationGainIn(c.points, c.last, at);
                c.held = true;
            }
            gain = c.heldGain;
        } else {
            gain = automationGainIn(c.points, c.last, at);
        }
        Smoothed& g = part_gain_[p];
        g.target = partSilenced(p) ? 0.0 : static_cast<double>(part_level_[p]) * static_cast<double>(gain);
        if (snap) g.value = g.target;
    }
}

void SongRenderer::setClickGain(float linear) noexcept { click_gain_.target = linear; }
void SongRenderer::setCueGain(float linear) noexcept { cue_gain_.target = linear; }
void SongRenderer::setPreviewGain(float linear) noexcept { preview_gain_ = linear; }

void SongRenderer::setSolo(int part) noexcept {
    solo_ = part >= 0 && part < part_count_ ? part : -1;   // a part the song doesn't have is no solo
    updatePartTargets();
}

void SongRenderer::render(SamplePos songTime, int frames, float* const* out, int numChannels, const float* envelope,
                          RenderParts what) noexcept {
    if ((song_ == nullptr && what.cue == nullptr) || !has_mix_ || frames <= 0 || click_.empty()) return;
    for (int b = 0; b < bus_count_; ++b) {   // each side of a stereo bus on its own: a channel the device lacks is skipped
        const int first = bus_first_[b];
        bus_left_[b] = first >= 0 && first < numChannels ? out[first] : nullptr;
        bus_right_[b] = bus_stereo_[b] && first + 1 >= 0 && first + 1 < numChannels ? out[first + 1] : nullptr;
    }
    for (int offset = 0; offset < frames; offset += max_block_)
        renderPiece(songTime + offset, std::min(max_block_, frames - offset), offset, envelope, what);
}

void SongRenderer::renderPiece(SamplePos songTime, int frames, int offset, const float* envelope,
                               RenderParts what) noexcept {
    if (song_ == nullptr) {
        renderCueAlone(frames, offset, what.cue);
        return;
    }
    const PreparedSong& song = *song_;
    const bool cueRouted = what.cue != nullptr && cues_role_ >= 0 && cues_role_ < role_rows_;

    // The count-in always clicks; pre-roll before it and the body only when the song's click is on; and a count-off
    // whatever the click. Rendered at unity: the click gain glides below.
    const bool clickRouted = what.click && click_role_ >= 0 && click_role_ < role_rows_;
    if (clickRouted) {
        std::fill_n(click_.data(), frames, 0.0f);
        const std::span<const ClickTick> all(song.ticks);
        const std::size_t first = song.click.enabled ? playable_first_ : count_in_first_;
        const std::size_t last = song.click.enabled ? all.size() : count_in_end_;
        const auto render = [&](std::size_t from, std::size_t to) {
            if (from >= to) return;
            const std::span<const ClickTick> run = all.subspan(from, to - from);
            if (song.clickAutomation.empty()) {
                renderClick(run, song.clickSound, song.click.accentDownbeat, songTime, frames, 1.0f, click_.data());
                return;
            }
            // Automated: each tick sounds whole at the gain the automation has at the tick's own time, so a step point
            // a few samples after a tick never cuts it. The count-in (ticks before bar 1) keeps the click's own level.
            const auto length = static_cast<SamplePos>(song.clickSound.beat.size());
            auto it = std::lower_bound(run.begin(), run.end(), songTime - length + 1,
                                       [](const ClickTick& t, SamplePos v) { return t.at < v; });
            for (; it != run.end() && it->at < songTime + frames; ++it) {
                const float gain = it->at < 0 ? 1.0f : automationGain(song.clickAutomation, it->at);
                if (gain != 0.0f)
                    renderClick(std::span<const ClickTick>(&*it, 1), song.clickSound, song.click.accentDownbeat, songTime, frames,
                                gain, click_.data());
            }
        };

        if (what.countOffTo > what.countOffFrom) {
            // The song's own ticks under the count-off are the bar before's, at its tempo: left out, the count-off's in
            // their place.
            const std::size_t offFirst = firstTickAt(what.countOffFrom), offLast = firstTickAt(what.countOffTo);
            render(first, std::min(last, offFirst));
            render(std::max(first, offLast), last);
            renderClick(what.countOffTicks, song.clickSound, true, songTime, frames, 1.0f, click_.data());
        } else {
            render(first, last);
        }
    }

    const SamplePos audioOffset = song.audioStartOffset;
    for (int i = 0; i < frames; ++i) {
        // Every smoothed value advances once per sample, before it is used, whatever the block (unless this call reads them
        // as they stand).
        if (what.advance && cells_moving_) stepCells();
        if (what.advance && automated_ > 0) followAutomation(songTime + i, snap_pending_);
        if (what.advance) snap_pending_ = false;
        if (what.advance && (parts_moving_ || automated_ > 0)) {
            bool moving = false;
            for (int p = 0; p < part_count_; ++p) {
                step(part_gain_[p]);
                moving = moving || part_gain_[p].value != part_gain_[p].target;
            }
            parts_moving_ = moving;
        }
        if (what.advance) {
            step(click_gain_);
            step(cue_gain_);
        }

        const int at = offset + i;
        const float env = envelope != nullptr ? envelope[at] : 1.0f;
        const float clickEnv =
            what.clickFollowsEnvelope ? env : (what.clickEnvelope != nullptr ? what.clickEnvelope[at] : 1.0f);
        const SamplePos audioPos = songTime + i + audioOffset;

        for (int p = 0; what.parts && p < part_count_; ++p) {
            const PartView& v = part_view_[p];
            if (v.role < 0 || audioPos < 0 || audioPos >= v.length) continue;
            const float gain = static_cast<float>(part_gain_[p].value);
            if (gain == 0.0f) continue;
            const auto s = static_cast<std::size_t>(audioPos);
            const float left = v.left[s];
            const float right = v.right != nullptr ? v.right[s] : left;
            const float mono = v.right != nullptr ? (left + right) * 0.5f : left;
            const Smoothed* row = &cells_[static_cast<std::size_t>(v.role) * kMaxBuses];
            for (int b = 0; b < bus_count_; ++b) {
                const float cell = static_cast<float>(row[b].value);
                if (cell == 0.0f) continue;
                const float g = gain * cell * env;
                if (bus_stereo_[b]) {
                    if (bus_left_[b] != nullptr) bus_left_[b][at] += left * g;
                    if (bus_right_[b] != nullptr) bus_right_[b][at] += right * g;
                } else if (bus_left_[b] != nullptr) {
                    bus_left_[b][at] += mono * g;
                }
            }
        }

        if (clickRouted) {
            const float click = click_[i] * static_cast<float>(click_gain_.value);
            if (click != 0.0f) {
                const Smoothed* row = &cells_[static_cast<std::size_t>(click_role_) * kMaxBuses];
                for (int b = 0; b < bus_count_; ++b) {
                    const float cell = static_cast<float>(row[b].value);
                    if (cell == 0.0f) continue;
                    const float v = click * (cell * clickEnv);
                    if (bus_left_[b] != nullptr) bus_left_[b][at] += v;
                    if (bus_right_[b] != nullptr) bus_right_[b][at] += v;
                }
            }
        }

        if (cueRouted) {   // not through the envelope: the voice fades by itself
            const float cue = what.cue[at];
            if (cue != 0.0f) {
                float level = static_cast<float>(cue_gain_.value);
                if (!song.cuesAutomation.empty()) level *= automationGain(song.cuesAutomation, songTime + i);
                if (what.advance) last_cue_level_ = level;
                mixCue(cue * level, at);
            }
        }
    }
}

void SongRenderer::renderCueAlone(int frames, int offset, const float* cue) noexcept {
    const bool routed = cue != nullptr && cues_role_ >= 0 && cues_role_ < role_rows_;
    for (int i = 0; i < frames; ++i) {
        if (cells_moving_) stepCells();
        const int at = offset + i;
        if (routed && cue[at] != 0.0f) mixCue(cue[at] * preview_gain_, at);
    }
}

void SongRenderer::stepCells() noexcept {
    bool moving = false;
    for (int r = 0; r < role_rows_; ++r) {
        Smoothed* row = &cells_[static_cast<std::size_t>(r) * kMaxBuses];
        for (int b = 0; b < bus_count_; ++b) {
            step(row[b]);
            moving = moving || row[b].value != row[b].target;
        }
    }
    cells_moving_ = moving;
}

void SongRenderer::mixCue(float value, int at) noexcept {
    const Smoothed* row = &cells_[static_cast<std::size_t>(cues_role_) * kMaxBuses];
    for (int b = 0; b < bus_count_; ++b) {
        const float cell = static_cast<float>(row[b].value);
        if (cell == 0.0f) continue;
        const float v = value * cell;
        if (bus_left_[b] != nullptr) bus_left_[b][at] += v;
        if (bus_right_[b] != nullptr) bus_right_[b][at] += v;
    }
}

} // namespace sqlive::engine
