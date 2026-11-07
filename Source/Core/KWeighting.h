#pragma once

#include <cmath>

namespace vintagevisual
{
constexpr double pi = 3.14159265358979323846;

struct BiquadCoefficients
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

struct KWeightingCoefficients
{
    BiquadCoefficients shelf;     // Stage 1: high shelf (head model)
    BiquadCoefficients highPass;  // Stage 2: RLB high-pass
};

// ITU-R BS.1770-4 K-weighting, redesigned for the real sample rate (bilinear transform).
// Message thread or audio thread; no allocation.
inline KWeightingCoefficients designKWeighting (double sampleRate)
{
    KWeightingCoefficients k;

    {
        constexpr double f0 = 1681.974450955533;
        constexpr double gainDb = 3.999843853973347;
        constexpr double q = 0.7071752369554196;

        const double kk = std::tan (pi * f0 / sampleRate);
        const double vh = std::pow (10.0, gainDb / 20.0);
        const double vb = std::pow (vh, 0.4996667741545416);
        const double a0 = 1.0 + kk / q + kk * kk;

        k.shelf.b0 = (vh + vb * kk / q + kk * kk) / a0;
        k.shelf.b1 = 2.0 * (kk * kk - vh) / a0;
        k.shelf.b2 = (vh - vb * kk / q + kk * kk) / a0;
        k.shelf.a1 = 2.0 * (kk * kk - 1.0) / a0;
        k.shelf.a2 = (1.0 - kk / q + kk * kk) / a0;
    }

    {
        constexpr double f0 = 38.13547087602444;
        constexpr double q = 0.5003270373238773;

        const double kk = std::tan (pi * f0 / sampleRate);
        const double a0 = 1.0 + kk / q + kk * kk;

        k.highPass.b0 = 1.0;
        k.highPass.b1 = -2.0;
        k.highPass.b2 = 1.0;
        k.highPass.a1 = 2.0 * (kk * kk - 1.0) / a0;
        k.highPass.a2 = (1.0 - kk / q + kk * kk) / a0;
    }

    return k;
}

// Transposed direct form II, double precision. Audio thread safe.
class Biquad final
{
public:
    void setCoefficients (const BiquadCoefficients& c) noexcept { coeffs = c; }
    void reset() noexcept { z1 = 0.0; z2 = 0.0; }

    double process (double x) noexcept
    {
        const double y = coeffs.b0 * x + z1;
        z1 = coeffs.b1 * x - coeffs.a1 * y + z2;
        z2 = coeffs.b2 * x - coeffs.a2 * y;

        // Avoid denormals in long silences.
        if (std::abs (z1) < 1.0e-30) z1 = 0.0;
        if (std::abs (z2) < 1.0e-30) z2 = 0.0;
        return y;
    }

private:
    BiquadCoefficients coeffs;
    double z1 = 0.0, z2 = 0.0;
};
} // namespace vintagevisual
