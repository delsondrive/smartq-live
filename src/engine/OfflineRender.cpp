#include "engine/OfflineRender.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace sqlive::engine {

std::vector<std::vector<float>> renderOffline(Engine& engine, int channels, std::int64_t frames, int blockFrames,
                                              std::vector<TimedAction> actions) {
    channels = std::max(0, channels);
    frames = std::max<std::int64_t>(0, frames);
    blockFrames = std::max(1, blockFrames);
    std::stable_sort(actions.begin(), actions.end(), [](const TimedAction& a, const TimedAction& b) { return a.at < b.at; });

    std::vector<std::vector<float>> out(static_cast<std::size_t>(channels), std::vector<float>(static_cast<std::size_t>(frames)));
    std::vector<float*> pointers(static_cast<std::size_t>(channels));
    std::size_t next = 0;
    std::int64_t pos = 0;
    for (;;) {
        while (next < actions.size() && actions[next].at <= pos) {
            if (actions[next].run) actions[next].run(engine);
            ++next;
        }
        if (pos >= frames) break;
        const std::int64_t until = next < actions.size() ? std::min(frames, actions[next].at) : frames;
        const int n = static_cast<int>(std::min<std::int64_t>(blockFrames, until - pos));
        for (std::size_t c = 0; c < pointers.size(); ++c) pointers[c] = out[c].data() + pos;
        engine.process(pointers.data(), channels, n);
        pos += n;
    }
    for (; next < actions.size(); ++next)   // at or after the end
        if (actions[next].run) actions[next].run(engine);
    return out;
}

} // namespace sqlive::engine
