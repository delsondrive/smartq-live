#include "ui/fx/Fft.h"

#include <cmath>
#include <numbers>
#include <utility>

namespace smartq::fx {

void RealFft::prepare(int size) {
    size_ = size;
    half_ = size / 2;
    reversed_.allocate(half_);
    for (int i = 0, j = 0; i < half_; ++i) {
        reversed_[i] = j;
        int bit = half_ >> 1;
        for (; (j & bit) != 0; bit >>= 1) j ^= bit;
        j ^= bit;
    }
    // Each one worked out on its own, rather than by turning the last, so that none carries the rounding of those before it.
    twiddles_.allocate(2 * half_);
    for (int k = 0; k < half_; ++k) {
        const double angle = 2.0 * std::numbers::pi * k / size;
        twiddles_[2 * k] = std::cos(angle);
        twiddles_[2 * k + 1] = std::sin(angle);
    }
    work_.allocate(2 * half_);
}

void RealFft::transform(bool inverse) noexcept {
    for (int i = 0; i < half_; ++i) {
        const int j = reversed_[i];
        if (i < j) {
            std::swap(work_[2 * i], work_[2 * j]);
            std::swap(work_[2 * i + 1], work_[2 * j + 1]);
        }
    }
    const double sign = inverse ? 1.0 : -1.0;
    for (int length = 2; length <= half_; length <<= 1) {
        const int halfLength = length / 2;
        const int stride = 2 * (half_ / length);  // in the table of the full size, which has the half size's twiddles at every other place
        for (int start = 0; start < half_; start += length) {
            for (int k = 0; k < halfLength; ++k) {
                const double wr = twiddles_[2 * k * stride];
                const double wi = sign * twiddles_[2 * k * stride + 1];
                const int a = 2 * (start + k);
                const int b = 2 * (start + k + halfLength);
                const double vr = work_[b] * wr - work_[b + 1] * wi;
                const double vi = work_[b] * wi + work_[b + 1] * wr;
                const double ur = work_[a], ui = work_[a + 1];
                work_[a] = ur + vr;
                work_[a + 1] = ui + vi;
                work_[b] = ur - vr;
                work_[b + 1] = ui - vi;
            }
        }
    }
}

void RealFft::forward(const double* in, double* out) noexcept {
    // The even samples as the real parts and the odd as the imaginary: z = e + i o.
    for (int n = 0; n < 2 * half_; ++n) work_[n] = in[n];
    transform(false);

    // E[k] = (Z[k] + conj Z[M - k]) / 2 and O[k] = (Z[k] - conj Z[M - k]) / 2i are the transforms of the even and odd samples, and
    // X[k] = E[k] + W^k O[k], with W = e^(-2 pi i / size). At k = M, W^k is -1.
    for (int k = 0; k <= half_; ++k) {
        const int p = k % half_;
        const int q = (half_ - k) % half_;
        const double zr = work_[2 * p], zi = work_[2 * p + 1];
        const double cr = work_[2 * q], ci = -work_[2 * q + 1];
        const double er = 0.5 * (zr + cr), ei = 0.5 * (zi + ci);
        const double or_ = 0.5 * (zi - ci), oi = -0.5 * (zr - cr);
        const double wr = k < half_ ? twiddles_[2 * k] : -1.0;
        const double wi = k < half_ ? -twiddles_[2 * k + 1] : 0.0;
        out[2 * k] = er + wr * or_ - wi * oi;
        out[2 * k + 1] = ei + wr * oi + wi * or_;
    }
}

void RealFft::inverse(const double* in, double* out) noexcept {
    // The same in reverse: E[k] = (X[k] + conj X[M - k]) / 2 and O[k] = (X[k] - conj X[M - k]) / 2 W^-k, and Z[k] = E[k] + i O[k].
    for (int k = 0; k < half_; ++k) {
        const int q = half_ - k;
        const double xr = in[2 * k], xi = k == 0 ? 0.0 : in[2 * k + 1];
        const double cr = in[2 * q], ci = q == half_ ? 0.0 : -in[2 * q + 1];
        const double er = 0.5 * (xr + cr), ei = 0.5 * (xi + ci);
        const double dr = 0.5 * (xr - cr), di = 0.5 * (xi - ci);
        const double wr = twiddles_[2 * k], wi = twiddles_[2 * k + 1];
        const double or_ = dr * wr - di * wi, oi = dr * wi + di * wr;
        work_[2 * k] = er - oi;
        work_[2 * k + 1] = ei + or_;
    }
    transform(true);
    const double scale = 1.0 / half_;
    for (int n = 0; n < 2 * half_; ++n) out[n] = scale * work_[n];
}

}  // namespace smartq::fx
