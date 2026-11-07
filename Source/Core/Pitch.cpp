#include "Pitch.h"

#include <algorithm>

namespace vintagevisual
{
void PitchDetector::prepare (double sampleRate, double minHz, double maxHz)
{
    decimation = std::max (1, (int) std::lround (sampleRate / 24000.0));
    effectiveRate = sampleRate / (double) decimation;
    tauMin = std::max (2, (int) std::floor (effectiveRate / maxHz));
    tauMax = std::max (tauMin + 2, (int) std::ceil (effectiveRate / minHz));
    requiredSamples = (windowSize + tauMax + 2) * decimation;

    signal.assign ((size_t) (windowSize + tauMax + 2), 0.0f);
    difference.assign ((size_t) tauMax + 2, 0.0f);
    normalised.assign ((size_t) tauMax + 2, 1.0f);
}

PitchResult PitchDetector::detect (const float* mono) noexcept
{
    const int n = (int) signal.size();

    // Boxcar decimation (cheap, good enough for pitch).
    double energy = 0.0;
    for (int i = 0; i < n; ++i)
    {
        float acc = 0.0f;
        for (int k = 0; k < decimation; ++k) acc += mono[i * decimation + k];
        const float v = acc / (float) decimation;
        signal[(size_t) i] = v;
        energy += (double) v * v;
    }

    PitchResult result;
    if (std::sqrt (energy / n) < 0.001) return result;  // below about -60 dBFS: nothing to tune

    // Difference function and its cumulative mean normalisation.
    double running = 0.0;
    normalised[0] = 1.0f;
    for (int tau = 1; tau <= tauMax + 1; ++tau)
    {
        double d = 0.0;
        for (int j = 0; j < windowSize; ++j)
        {
            const double delta = (double) signal[(size_t) j] - (double) signal[(size_t) (j + tau)];
            d += delta * delta;
        }
        difference[(size_t) tau] = (float) d;
        running += d;
        normalised[(size_t) tau] = running > 0.0 ? (float) (d * tau / running) : 1.0f;
    }

    constexpr float threshold = 0.15f, fallbackThreshold = 0.30f;
    int best = -1;

    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        if (normalised[(size_t) tau] < threshold)
        {
            while (tau + 1 <= tauMax && normalised[(size_t) tau + 1] < normalised[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    }

    if (best < 0)  // no clear dip: accept the global minimum only if it is reasonably deep
    {
        float lowest = 1.0e9f;
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (normalised[(size_t) tau] < lowest) { lowest = normalised[(size_t) tau]; best = tau; }
        if (lowest > fallbackThreshold) return result;
    }

    // Parabolic interpolation around the minimum.
    double tauRefined = best;
    if (best > tauMin && best < tauMax)
    {
        const double a = normalised[(size_t) best - 1], b = normalised[(size_t) best], c = normalised[(size_t) best + 1];
        const double denom = a - 2.0 * b + c;
        if (std::abs (denom) > 1.0e-12) tauRefined += 0.5 * (a - c) / denom;
    }

    result.valid = true;
    result.frequencyHz = effectiveRate / tauRefined;
    result.confidence = std::clamp (1.0 - (double) normalised[(size_t) best], 0.0, 1.0);
    return result;
}
} // namespace vintagevisual
