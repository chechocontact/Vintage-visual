#include "TruePeakDetector.h"
#include "KWeighting.h"

#include <algorithm>
#include <cmath>

namespace vintagevisual
{
double linearToDb (double linear) noexcept
{
    return linear > 1.0e-6 ? 20.0 * std::log10 (linear) : -120.0;
}

void TruePeakDetector::prepare (double sampleRate, int numChannels)
{
    factor = sampleRate < 96000.0 ? 4 : (sampleRate < 192000.0 ? 2 : 1);
    numCh = numChannels;

    const int taps = 12 * factor + 1;
    const double centre = 6.0 * factor;

    kernel.assign ((size_t) (factor * historyLength), 0.0);

    for (int p = 0; p < factor; ++p)
    {
        double sum = 0.0;

        for (int k = 0; k < historyLength; ++k)
        {
            const int n = factor * k + p;
            if (n >= taps) continue;

            const double x = ((double) n - centre) / (double) factor;
            const double s = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (pi * x) / (pi * x);
            const double w = taps > 1 ? 0.5 * (1.0 - std::cos (2.0 * pi * n / (taps - 1))) : 1.0;
            const double v = s * w;
            kernel[(size_t) (p * historyLength + k)] = v;
            sum += v;
        }

        if (p != 0 && sum != 0.0)  // unity DC gain per phase (phase 0 is the identity)
            for (int k = 0; k < historyLength; ++k)
                kernel[(size_t) (p * historyLength + k)] /= sum;
    }

    history.assign ((size_t) (numCh * 2 * historyLength), 0.0);
    pos.assign ((size_t) numCh, 0);
    peak = 0.0;
}

void TruePeakDetector::reset() noexcept
{
    std::fill (history.begin(), history.end(), 0.0);
    std::fill (pos.begin(), pos.end(), 0);
    peak = 0.0;
}

void TruePeakDetector::process (const float* const* channels, int numSamples) noexcept
{
    for (int c = 0; c < numCh; ++c)
    {
        double* h = history.data() + (size_t) c * 2 * historyLength;
        int& w = pos[(size_t) c];
        double localPeak = peak;

        for (int i = 0; i < numSamples; ++i)
        {
            double x = (double) channels[c][i];
            if (! std::isfinite (x)) x = 0.0;

            w = (w == 0 ? historyLength - 1 : w - 1);
            h[w] = x;
            h[w + historyLength] = x;

            localPeak = std::max (localPeak, std::abs (x));

            if (factor > 1)
            {
                for (int p = 1; p < factor; ++p)
                {
                    const double* kp = kernel.data() + (size_t) p * historyLength;
                    double acc = 0.0;
                    for (int k = 0; k < historyLength; ++k)
                        acc += kp[k] * h[w + k];
                    localPeak = std::max (localPeak, std::abs (acc));
                }
            }
        }

        peak = localPeak;
    }
}
} // namespace vintagevisual
