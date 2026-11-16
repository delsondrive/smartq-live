#pragma once

#include <cstddef>
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

// How much of each role reaches each output bus. Built on the control thread, read by the audio thread.
struct MixSetup {
    std::vector<OutputBus> buses;
    int roleCount = 0;
    int clickRole = -1, cuesRole = -1, padRole = -1;
    std::vector<float> gain;                       // roleCount × buses.size(), row-major by role, linear, 0 = not sent

    float at(int role, int bus) const noexcept {   // 0 for anything out of range
        const auto busCount = buses.size();
        if (role < 0 || role >= roleCount || bus < 0 || static_cast<std::size_t>(bus) >= busCount) return 0.0f;
        const std::size_t i = static_cast<std::size_t>(role) * busCount + static_cast<std::size_t>(bus);
        return i < gain.size() ? gain[i] : 0.0f;
    }
};

} // namespace sqlive::engine
