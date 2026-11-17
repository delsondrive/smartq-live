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
    cells_.assign(static_cast<std::size_t>(kMaxRoles) * kMaxBuses, {});
    click_gain_ = {};
    song_ = nullptr;
    part_count_ = 0;
    solo_ = -1;
    count_in_first_ = count_in_end_ = 0;
    parts_moving_ = false;
    has_mix_ = false;
    bus_count_ = 0;
    role_rows_ = 0;
    click_role_ = -1;
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

    const float clickGain = song != nullptr ? song->click.gain : 1.0f;
    click_gain_ = {clickGain, clickGain};
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
    has_mix_ = true;
}

void SongRenderer::updatePartTargets() noexcept {
    for (int p = 0; p < part_count_; ++p) {
        const bool silenced = part_mute_[p] != 0 || (solo_ >= 0 && solo_ != p);
        part_gain_[p].target = silenced ? 0.0 : static_cast<double>(part_level_[p]);
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

void SongRenderer::setClickGain(float linear) noexcept { click_gain_.target = linear; }

void SongRenderer::setSolo(int part) noexcept {
    solo_ = part >= 0 && part < part_count_ ? part : -1;   // a part the song doesn't have is no solo
    updatePartTargets();
}

void SongRenderer::render(SamplePos songTime, int frames, float* const* out, int numChannels, const float* envelope,
                          RenderParts what) noexcept {
    if (song_ == nullptr || !has_mix_ || frames <= 0 || click_.empty()) return;
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
    const PreparedSong& song = *song_;

    // The count-in always clicks; pre-roll before it and the body only when the song's click is on; and a forced range
    // whatever the click. Rendered at unity: the click gain glides below.
    const bool clickRouted = what.click && click_role_ >= 0 && click_role_ < role_rows_;
    if (clickRouted) {
        std::fill_n(click_.data(), frames, 0.0f);
        const std::span<const ClickTick> all(song.ticks);
        std::size_t first = song.click.enabled ? playable_first_ : count_in_first_;
        std::size_t last = song.click.enabled ? all.size() : count_in_end_;
        const auto render = [&](std::size_t from, std::size_t to) {
            renderClick(all.subspan(from, to - from), song.clickSound, song.click.accentDownbeat, songTime, frames, 1.0f,
                        click_.data());
        };
        if (what.forcedClickTo > what.forcedClickFrom) {
            const std::size_t forcedFirst = firstTickAt(what.forcedClickFrom);
            const std::size_t forcedLast = firstTickAt(what.forcedClickTo);
            if (first >= last) {                                    // nothing else clicks
                first = forcedFirst;
                last = forcedLast;
            } else if (forcedFirst <= last && first <= forcedLast) {   // overlapping or touching: one run, no tick twice
                first = std::min(first, forcedFirst);
                last = std::max(last, forcedLast);
            } else if (forcedFirst < forcedLast) {
                render(forcedFirst, forcedLast);
            }
        }
        if (first < last) render(first, last);
    }

    const SamplePos audioOffset = song.audioStartOffset;
    for (int i = 0; i < frames; ++i) {
        // Every smoothed value advances once per sample, before it is used, whatever the block (unless this call reads them
        // as they stand).
        if (what.advance && cells_moving_) {
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
        if (what.advance && parts_moving_) {
            bool moving = false;
            for (int p = 0; p < part_count_; ++p) {
                step(part_gain_[p]);
                moving = moving || part_gain_[p].value != part_gain_[p].target;
            }
            parts_moving_ = moving;
        }
        if (what.advance) step(click_gain_);

        const int at = offset + i;
        const float env = envelope != nullptr ? envelope[at] : 1.0f;
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
                    const float v = click * (cell * env);
                    if (bus_left_[b] != nullptr) bus_left_[b][at] += v;
                    if (bus_right_[b] != nullptr) bus_right_[b][at] += v;
                }
            }
        }
    }
}

} // namespace sqlive::engine
