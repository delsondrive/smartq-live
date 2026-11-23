#include "dsp/UpwardLift.h"

#include <algorithm>
#include <cmath>

#include "dsp/ModuleSupport.h"
#include "dsp/TransientShaping.h"

namespace smartq::dsp {
namespace {

constexpr double kMeanMs = 50.0;
constexpr double kLevelRiseMs = 300.0, kLevelFallMs = 3000.0;
constexpr double kReferenceRiseMs = 1000.0, kReferenceFallMs = 120000.0;
constexpr double kTaperRiseMs = 50.0, kTaperFallMs = 300.0;
constexpr double kLiftRiseMs = 1000.0, kLiftFallMs = 100.0;

// Nothing is lifted within this much of the loud parts, and half of every dB further under them is.
constexpr double kLeftAloneDb = 2.0;
constexpr double kLiftPerDb = 0.5;

// The taper: whole until its level is this far under the loud parts, nothing from this far, in a straight line in dB between.
constexpr double kTaperFromDb = 30.0, kTaperToDb = 40.0;

// The powers these come to, so that a sample that is nowhere near them costs comparisons and no logarithm.
constexpr double kLeftAloneRatio = 1.5848931924611136;  // 10^(2 / 10)
constexpr double kTaperFromRatio = 1.0e3, kTaperToRatio = 1.0e4;
constexpr double kQuietestPower = 1.0e-8;               // -80 dBFS
constexpr double kPowerFloor = kLevelFloor * kLevelFloor;

double powerDb(double ratio) noexcept { return 10.0 * std::log10(ratio); }

}  // namespace

void UpwardLift::prepare(double sampleRate) noexcept {
    meanStep_ = coefficientFor(kMeanMs, sampleRate);
    levelRise_ = coefficientFor(kLevelRiseMs, sampleRate);
    levelFall_ = coefficientFor(kLevelFallMs, sampleRate);
    referenceRise_ = coefficientFor(kReferenceRiseMs, sampleRate);
    referenceFall_ = coefficientFor(kReferenceFallMs, sampleRate);
    taperRise_ = coefficientFor(kTaperRiseMs, sampleRate);
    taperFall_ = coefficientFor(kTaperFallMs, sampleRate);
    liftRise_ = coefficientFor(kLiftRiseMs, sampleRate);
    liftFall_ = coefficientFor(kLiftFallMs, sampleRate);
    reset();
}

void UpwardLift::reset() noexcept {
    mean_ = 0.0;
    level_ = 0.0;
    reference_ = 0.0;
    taperLevel_ = 0.0;
    liftDb_ = 0.0;
}

double UpwardLift::hear(double level, double upwardDb) noexcept {
    const double power = level * level;
    mean_ = transient::followed(mean_, power, meanStep_, meanStep_);
    level_ = transient::followed(level_, mean_, levelRise_, levelFall_);
    reference_ = transient::followed(reference_, level_, referenceRise_, referenceFall_);
    taperLevel_ = transient::followed(taperLevel_, power, taperRise_, taperFall_);

    // At Upward 0 the aim is 0 whatever is heard, and so is it while the level is within 2 dB of the loud parts, the taper's level
    // is 40 dB or more under them, or it is under -80 dBFS.
    double aimDb = 0.0;
    if (upwardDb > 0.0 && taperLevel_ >= kQuietestPower && reference_ > kLeftAloneRatio * level_ && reference_ < kTaperToRatio * taperLevel_) {
        const double taper = reference_ <= kTaperFromRatio * taperLevel_ ? 1.0 : (kTaperToDb - powerDb(reference_ / taperLevel_)) / (kTaperToDb - kTaperFromDb);
        const double underDb = powerDb(reference_ / std::max(level_, kPowerFloor));
        aimDb = std::min(upwardDb, kLiftPerDb * std::max(0.0, underDb - kLeftAloneDb)) * limited(taper, 0.0, 1.0);
    }
    // Never more than Upward is now, so that turning it down takes the lift down with it.
    liftDb_ = std::min(moved(liftDb_, aimDb, liftRise_, liftFall_), std::max(upwardDb, 0.0));
    return liftDb_;
}

}  // namespace smartq::dsp
