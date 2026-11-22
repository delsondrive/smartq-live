#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

namespace sqlive::engine {

inline constexpr int kMaxRoles = 64;
inline constexpr int kMaxBuses = 32;

// One output of the device: a mono channel or a stereo pair starting at firstChannel (0-based).
struct OutputBus {
    int firstChannel = 0;
    bool stereo = true;
    float trim = 1.0f;
};

// A pan's (left, right) gains, by the balance law: the pan runs from -100 (hard left) to 100 (hard right), 0 the centre,
// limited to that range. The side the sound moves towards stays at unity; the far side falls along a cosine to silence:
// cos(|pan| / 100 × π/2), exactly 0 at ±100. The centre is exactly (1, 1), so a centred part is multiplied by exactly 1 and
// sounds bit for bit as it would with no pan at all. A stereo part keeps its image (each side scaled); a mono one is
// placed the same way. Allocates nothing; std::cos runs only off the centre.
inline std::pair<float, float> balanceGains(int pan) noexcept {
    const int p = std::clamp(pan, -100, 100);
    if (p == 0) return {1.0f, 1.0f};
    const float farSide = std::abs(p) == 100
                          ? 0.0f
                          : static_cast<float>(std::cos(static_cast<double>(std::abs(p)) / 100.0 * (std::numbers::pi / 2.0)));
    return p < 0 ? std::pair<float, float>{1.0f, farSide} : std::pair<float, float>{farSide, 1.0f};
}

// How much of each role reaches each output bus, and where it sits there. Built on the control thread, read by the audio
// thread. pan and panOwn are indexed as gain; either may be empty (or short), which reads as centre and following the
// song: an output's own pan for a role (panOwn 1) is used for every part of that role instead of the song's pan of the
// part, and for the click and the cues, which have no song pan, it is their only pan (centre when they follow). A mono
// bus ignores pan.
struct MixSetup {
    std::vector<OutputBus> buses;
    int roleCount = 0;
    int clickRole = -1, cuesRole = -1, padRole = -1;
    std::vector<float> gain;                       // roleCount × buses.size(), row-major by role, linear, 0 = not sent
    std::vector<int> pan;                          // the same cells: the role's own pan on that bus, -100 to 100
    std::vector<std::uint8_t> panOwn;              // the same cells: 1 = the own pan applies, 0 = the song's pan does

    float at(int role, int bus) const noexcept {   // 0 for anything out of range
        const std::size_t i = cell(role, bus);
        return i < gain.size() ? gain[i] : 0.0f;
    }
    int panAt(int role, int bus) const noexcept {  // 0 (centre) for anything out of range
        const std::size_t i = cell(role, bus);
        return i < pan.size() ? pan[i] : 0;
    }
    bool panOwnAt(int role, int bus) const noexcept {   // false (follows the song) for anything out of range
        const std::size_t i = cell(role, bus);
        return i < panOwn.size() && panOwn[i] != 0;
    }

private:
    std::size_t cell(int role, int bus) const noexcept {   // SIZE_MAX out of range
        const auto busCount = buses.size();
        if (role < 0 || role >= roleCount || bus < 0 || static_cast<std::size_t>(bus) >= busCount) return SIZE_MAX;
        return static_cast<std::size_t>(role) * busCount + static_cast<std::size_t>(bus);
    }
};

} // namespace sqlive::engine
