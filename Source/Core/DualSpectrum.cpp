#include "DualSpectrum.h"

#include <algorithm>
#include <cmath>

namespace vintagevisual
{
void DualSpectrum::prepare (double sampleRate, int numBands, int longOrder, int shortOrder, double crossLowHz, double crossHighHz)
{
    longAnalyzer.prepare (sampleRate, longOrder, numBands);
    shortAnalyzer.prepare (sampleRate, shortOrder, numBands);
    longSize = longAnalyzer.getFftSize();
    shortSize = shortAnalyzer.getFftSize();

    merged.assign ((size_t) numBands, SpectrumAnalyzer::floorDb);
    longWeight.assign ((size_t) numBands, 0.0f);

    for (int b = 0; b < numBands; ++b)
    {
        const double f = longAnalyzer.getBandFrequency (b);
        double w = 0.0;
        if (f <= crossLowHz) w = 1.0;
        else if (f < crossHighHz) w = 1.0 - std::log (f / crossLowHz) / std::log (crossHighHz / crossLowHz);
        longWeight[(size_t) b] = (float) w;
    }
}

void DualSpectrum::analyse (const float* mono) noexcept
{
    longAnalyzer.analyse (mono);
    shortAnalyzer.analyse (mono + (longSize - shortSize));

    const auto& l = longAnalyzer.getBandsDb();
    const auto& s = shortAnalyzer.getBandsDb();

    for (size_t b = 0; b < merged.size(); ++b)
    {
        const float w = longWeight[b];
        merged[b] = w >= 1.0f ? l[b] : (w <= 0.0f ? s[b] : w * l[b] + (1.0f - w) * s[b]);
    }
}
} // namespace vintagevisual
