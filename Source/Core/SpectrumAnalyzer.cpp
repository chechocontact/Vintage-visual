#include "SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace vintagevisual
{
void SpectrumAnalyzer::prepare (double sampleRate, int fftOrder, int numBands, double minHz, double maxHz)
{
    fftSize = 1 << fftOrder;
    fft = std::make_unique<Fft> (fftOrder);

    window.assign ((size_t) fftSize, 0.0f);
    double sum = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (double) i / (double) fftSize);
        window[(size_t) i] = (float) w;
        sum += w;
    }
    gain = (float) (2.0 / sum);  // peak bin magnitude * gain = sine amplitude

    re.assign ((size_t) fftSize, 0.0f);
    im.assign ((size_t) fftSize, 0.0f);
    magnitude.assign ((size_t) fftSize / 2, 0.0f);
    bandsDb.assign ((size_t) numBands, floorDb);
    centres.assign ((size_t) numBands, 0.0);
    bands.assign ((size_t) numBands, {});

    maxHz = std::min (maxHz, sampleRate * 0.475);
    const double binHz = sampleRate / (double) fftSize;
    const double ratio = std::pow (maxHz / minHz, 1.0 / (double) (numBands - 1));
    const double halfStep = std::sqrt (ratio);
    const int lastValid = fftSize / 2 - 1;

    for (int i = 0; i < numBands; ++i)
    {
        const double f = minHz * std::pow (ratio, (double) i);
        centres[(size_t) i] = f;

        Band b;
        b.centreBin = f / binHz;
        const double lo = f / halfStep / binHz, hi = f * halfStep / binHz;
        b.firstBin = std::clamp ((int) std::ceil (lo), 1, lastValid);
        b.lastBin = std::clamp ((int) std::floor (hi), 1, lastValid);
        if (hi - lo < 1.0) b.lastBin = b.firstBin - 1;  // narrower than a bin: interpolate
        bands[(size_t) i] = b;
    }
}

void SpectrumAnalyzer::analyse (const float* mono) noexcept
{
    for (int i = 0; i < fftSize; ++i)
    {
        re[(size_t) i] = mono[i] * window[(size_t) i];
        im[(size_t) i] = 0.0f;
    }

    fft->forward (re.data(), im.data());

    const int half = fftSize / 2;
    for (int k = 0; k < half; ++k)
        magnitude[(size_t) k] = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]) * gain;

    for (size_t i = 0; i < bands.size(); ++i)
    {
        const auto& b = bands[i];
        float m = 0.0f;

        if (b.lastBin < b.firstBin)
        {
            const int k0 = std::clamp ((int) std::floor (b.centreBin), 1, half - 2);
            const float t = (float) std::clamp (b.centreBin - (double) k0, 0.0, 1.0);
            m = magnitude[(size_t) k0] * (1.0f - t) + magnitude[(size_t) k0 + 1] * t;
        }
        else
        {
            for (int k = b.firstBin; k <= b.lastBin; ++k)
                m = std::max (m, magnitude[(size_t) k]);
        }

        bandsDb[i] = m > 1.0e-6f ? 20.0f * std::log10 (m) : floorDb;
    }
}
} // namespace vintagevisual
