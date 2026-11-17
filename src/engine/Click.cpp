#include "engine/Click.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sqlive::engine {
namespace {

int ticksPerBeat(Subdivision s) noexcept {
    switch (s) {
        case Subdivision::Eighths: return 2;
        case Subdivision::Triplets: return 3;
        case Subdivision::Sixteenths: return 4;
        case Subdivision::None: break;
    }
    return 1;
}

std::vector<float> makeTone(double freq, double db, double sampleRate, std::size_t length) {
    constexpr double seconds = 0.030;
    constexpr double fadeIn = 0.001;
    const double decay = std::log(1000.0) / seconds;   // -60 dB at 30 ms
    const double amp = std::pow(10.0, db / 20.0);
    std::vector<float> v(length);
    for (std::size_t i = 0; i < length; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double env = std::exp(-decay * t) * std::min(1.0, t / fadeIn);
        v[i] = static_cast<float>(amp * env * std::sin(2.0 * std::numbers::pi * freq * t));
    }
    return v;
}

} // namespace

std::vector<ClickTick> buildClickTicks(const TempoMap& map, double sampleRate, SamplePos from, SamplePos to,
                                       Subdivision subdivision) {
    std::vector<ClickTick> ticks;
    if (!(sampleRate > 0.0) || to <= from) return ticks;
    const int per = ticksPerBeat(subdivision);
    // Start one bar early: rounding may put a bar's last tick on the other side of a boundary. Ticks are filtered below.
    int bar = map.barBeatAt(static_cast<double>(from) / sampleRate).bar - 1;
    for (;; ++bar) {
        if (map.sampleAt({bar, 1.0}, sampleRate) >= to) break;
        const int beats = map.beatsInBar(bar);
        for (int b = 0; b < beats; ++b) {
            for (int k = 0; k < per; ++k) {
                const SamplePos at = map.sampleAt({bar, 1.0 + b + static_cast<double>(k) / per}, sampleRate);
                if (at < from || at >= to) continue;
                const ClickKind kind = k != 0 ? ClickKind::Subdivision : (b == 0 ? ClickKind::Downbeat : ClickKind::Beat);
                ticks.push_back({at, kind});
            }
        }
    }
    return ticks;
}

ClickSound makeClickSound(int sound, double sampleRate) {
    struct Freqs { double downbeat, beat, subdivision; };
    Freqs f{1600.0, 1000.0, 1000.0};
    if (sound == 1) f = {2000.0, 1500.0, 1500.0};
    else if (sound == 2) f = {2600.0, 1800.0, 1800.0};
    const auto length = static_cast<std::size_t>(std::llround(0.030 * sampleRate));
    ClickSound s;
    s.downbeat = makeTone(f.downbeat, -1.0, sampleRate, length);
    s.beat = makeTone(f.beat, -1.0, sampleRate, length);
    s.subdivision = makeTone(f.subdivision, -9.0, sampleRate, length);
    return s;
}

ClickSound clickSoundFromClip(std::span<const float> mono, double sampleRate) {
    ClickSound s;
    if (!(sampleRate > 0.0)) return s;
    const auto longest = static_cast<std::size_t>(std::llround(0.100 * sampleRate));
    const std::size_t length = std::min(mono.size(), longest);
    const std::size_t fade = std::min(length, static_cast<std::size_t>(std::llround(0.005 * sampleRate)));
    s.downbeat.assign(mono.begin(), mono.begin() + static_cast<std::ptrdiff_t>(length));
    for (std::size_t i = length - fade; i < length; ++i)   // falls in a straight line to 0 at the last sample
        s.downbeat[i] *= static_cast<float>(static_cast<double>(length - 1 - i) / static_cast<double>(fade));
    const auto scaled = [&](double db) {
        const auto gain = static_cast<float>(std::pow(10.0, db / 20.0));
        std::vector<float> v(s.downbeat);
        for (float& x : v) x *= gain;
        return v;
    };
    s.beat = scaled(-4.0);
    s.subdivision = scaled(-10.0);
    return s;
}

void renderClick(std::span<const ClickTick> ticks, const ClickSound& sound, bool accentDownbeat,
                 SamplePos from, int frames, float gain, float* out) noexcept {
    const auto length = static_cast<SamplePos>(sound.beat.size());
    if (frames <= 0 || length == 0) return;
    const SamplePos end = from + frames;
    auto it = std::lower_bound(ticks.begin(), ticks.end(), from - length + 1,
                               [](const ClickTick& t, SamplePos v) { return t.at < v; });
    for (; it != ticks.end() && it->at < end; ++it) {
        const std::vector<float>& s = it->kind == ClickKind::Subdivision ? sound.subdivision
                                    : (it->kind == ClickKind::Downbeat && accentDownbeat) ? sound.downbeat
                                    : sound.beat;
        const SamplePos first = std::max<SamplePos>(it->at, from);
        const SamplePos last = std::min<SamplePos>(it->at + length, end);
        for (SamplePos p = first; p < last; ++p)
            out[p - from] += gain * s[static_cast<std::size_t>(p - it->at)];
    }
}

} // namespace sqlive::engine
