#pragma once

#include "dsp/ModuleSupport.h"

// Copied from Smart Q (smarteq_vst src/dsp/Fft.h) for the page analyzer (PageAnalyzer.cpp), which reads the chain's tap on the
// message thread. In namespace smartq::fx, not smartq::dsp, so that it can never clash with a copy in src/dsp.
namespace smartq::fx {

// A real FFT in 64 bits, radix 2, with every table made by prepare. Nothing allocates after it. The size reals are packed in pairs
// into a complex transform of half the size, which is then unpacked into the size / 2 + 1 bins of the real one. JUCE's FFT is 32-bit,
// and the DSP library has no JUCE in it.
class RealFft {
public:
    void prepare(int size);  // a power of two from 64 to 65536

    // size reals in, size / 2 + 1 bins out, each as its real part then its imaginary part: size + 2 values. Forward is e^-i, and
    // not scaled.
    void forward(const double* in, double* outInterleavedComplex) noexcept;

    // Scaled so that inverse(forward(x)) == x. The imaginary parts of the first and last bins are taken to be zero, as a real
    // signal's are.
    void inverse(const double* inInterleavedComplex, double* out) noexcept;

    int size() const noexcept { return size_; }

private:
    // The complex transform of half the size, in place in work_: e^-i forward, e^+i inverse, not scaled.
    void transform(bool inverse) noexcept;

    int size_ = 0, half_ = 0;
    dsp::Preallocated<int> reversed_;     // each of the half-size transform's places, with its bits reversed
    dsp::Preallocated<double> twiddles_;  // cos and sin of 2 pi k / size, for k below half the size
    dsp::Preallocated<double> work_;      // half the size of complex values, real then imaginary
};

}  // namespace smartq::fx
