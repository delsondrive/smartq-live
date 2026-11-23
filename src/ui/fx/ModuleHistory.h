#pragma once

#include <array>
#include <atomic>

#include "ui/fx/params/Parameters.h"
#include "ui/fx/AudioIdle.h"
#include "ui/fx/PageLook.h"

namespace smartq {

class ChainHost;

// The last six seconds of one module that reportsReduction or reportsBoost, for its page, its card and its miniature to draw. The
// chain column makes one for each such slot and steps those in the chain, which is the only place in the window that their meters
// are read, so the page, the card and the miniature always show the same thing. A step takes the largest level, reduction and boost
// since the last, as the module's meters for the window hold them, and adds as many columns as the time that has gone by calls for,
// so that six seconds are six seconds at any rate the window's timer manages.
//
// What the meters hold means nothing while no block is arriving, and a module that is bypassed isn't turning anything down, so
// either of those reads as no reduction. Once all of it has gone quiet, or all of it looks the same, it stops moving, and version()
// with it. Message thread.
class ModuleHistory {
public:
    static constexpr double kSeconds = 6.0;
    static constexpr double kColumnSeconds = 1.0 / 30.0;
    static constexpr int kColumns = 180;
    static constexpr float kFloorDb = -120.0f;

    // A reduction under this is less than a pixel of it.
    static constexpr float kQuietReductionDb = 0.05f;

    // The foot of the scale a type's page and miniature draw its history on: Dynamic's 60 dB below full scale, the whole of Gate's
    // threshold, and Limit's 60 dB from +12. A level under it is below what's drawn, so it counts as quiet, and two under it look the
    // same.
    static constexpr double scaleBottomDb(ModuleType type) noexcept {
        switch (type) {
            case ModuleType::Gate: return -80.0;
            case ModuleType::Limit: return -48.0;
            case ModuleType::Eq:
            case ModuleType::Dynamic:
            case ModuleType::DeEss:
            case ModuleType::Drive:
            case ModuleType::Width:
            case ModuleType::Multiband:
            case ModuleType::Transient:
            case ModuleType::Resonance: break;
        }
        return -60.0;
    }

    // The least reduction a reading tells, on the page and on the card.
    static constexpr double kReadingFloorDb = 0.1;

    struct Column {
        float levelDb = kFloorDb;   // what the detector heard: a Limit's after its input gain
        float outputDb = kFloorDb;  // what it left going out: kFloorDb while it listens to the sidechain, whose level says nothing of that.
                                    // A Transient's is its measured peak, in dBFS
        float reductionDb = 0.0f;   // 0 or above
        float boostDb = 0.0f;       // 0 or above, for a type that reportsBoost: how far it turned the track up by its own action
    };

    // `slot` is of a type that reportsReduction or reportsBoost.
    ModuleHistory(ChainHost&, int slot);

    // Starts again from nothing, with whatever the meters held since they were last read thrown away. For a module that has come
    // into the chain.
    void restart() noexcept;

    // One step of the display, with the time since the last one.
    void step(double dtSeconds) noexcept;

    // Counts the steps where the columns moved, so that nothing asks for a repaint without one.
    unsigned version() const noexcept { return version_; }

    // 0 is the oldest, kColumns - 1 the newest.
    const Column& column(int index) const noexcept { return columns_[static_cast<size_t>((newest_ + 1 + index) % kColumns)]; }

    double levelNowDb() const noexcept { return now_.levelDb; }
    double reductionNowDb() const noexcept { return now_.reductionDb; }
    double boostNowDb() const noexcept { return now_.boostDb; }
    double outputNowDb() const noexcept { return now_.outputDb; }

    // Whether blocks were arriving at the latest step.
    bool audioRunning() const noexcept { return running_; }

private:
    // What comes out for what the detector heard and the reduction, by the type's own law.
    float outputDbFor(float levelDb, float reductionDb, bool bypassed) const noexcept;

    ChainHost& processor_;
    const int slot_;
    const ModuleType type_;
    const bool boosts_;         // whether the type reportsBoost
    const bool measuresOutput_;  // whether the processor measures what it puts out: a Transient's, for its page's warning
    const float quietLevelDb_;  // the type's scaleBottomDb
    ControlValues values_{};
    std::atomic<float>* moduleBypass_ = nullptr;
    std::atomic<float>* pluginBypass_ = nullptr;
    AudioIdle audioIdle_;
    bool running_ = false;

    std::array<Column, kColumns> columns_{};
    int newest_ = kColumns - 1;
    int loudColumns_ = 0;  // how many of them have something to draw
    Column steady_{};      // the newest columns all look like this,
    int steadyColumns_ = 0;  // and this many of them, up to all
    Column now_{};
    double owedSeconds_ = 0.0;
    unsigned version_ = 0;
};

// The big reading of how far a module is turning the track down, as its page shows it: the most since the last reading, read ten
// times a second, which is quick enough to follow and slow enough to read, and still tells a short peak between readings.
class ReductionReading {
public:
    static constexpr double kSeconds = kReadingSeconds;

    // True when what it says has changed.
    bool step(double reductionNowDb, double dtSeconds);

    // What it says is out of date: the next step reads afresh.
    void restart() noexcept;

    const juce::String& text() const noexcept { return text_; }

    // "-5.2 dB", or "0.0 dB" for less than kReadingFloorDb.
    static juce::String textFor(double reductionDb);

private:
    double seconds_ = 0.0, peakDb_ = 0.0;
    juce::String text_{"0.0 dB"};
};

// The same for a module that reportsBoost, which turns the track up as well as down: whichever it did more of since the last reading,
// signed, so that "+4.8 dB" is added and "-2.1 dB" taken away.
class GainReading {
public:
    static constexpr double kSeconds = kReadingSeconds;

    // True when what it says has changed.
    bool step(double boostNowDb, double reductionNowDb, double dtSeconds);

    // What it says is out of date: the next step reads afresh.
    void restart() noexcept;

    const juce::String& text() const noexcept { return text_; }

    // What it says was done: nothing, as at "0.0 dB", something added, or something taken away.
    enum class Way { Nothing, Added, TakenAway };
    Way way() const noexcept { return way_; }

    // "+4.8 dB" or "-2.1 dB", whichever is more, or "0.0 dB" when both are under kReadingFloorDb.
    static juce::String textFor(double boostDb, double reductionDb);

private:
    double seconds_ = 0.0, boostDb_ = 0.0, reductionDb_ = 0.0;
    juce::String text_{"0.0 dB"};
    Way way_ = Way::Nothing;
};

}  // namespace smartq
