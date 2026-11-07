#pragma once

#include "SpectrumAnalyzer.h"

#include <vector>

namespace vintagevisual
{
// Spectrogram column built from TWO analysis resolutions:
// a long FFT for the bass (fine frequency detail) and a short FFT for the rest (fine time detail).
// Bands below `crossLowHz` come from the long FFT, above `crossHighHz` from the short one, with a
// log-frequency cross-fade in between. UI thread only (allocates in prepare()).
class DualSpectrum final
{
public:
    void prepare (double sampleRate, int numBands, int longOrder = 14, int shortOrder = 11,
                  double crossLowHz = 300.0, double crossHighHz = 600.0);

    int getLongSize() const noexcept { return longSize; }
    int getShortSize() const noexcept { return shortSize; }
    int getNumBands() const noexcept { return longAnalyzer.getNumBands(); }
    double getBandFrequency (int band) const noexcept { return longAnalyzer.getBandFrequency (band); }
    const std::vector<float>& getBandsDb() const noexcept { return merged; }

    // `mono` holds getLongSize() samples, oldest first. The short FFT reads the newest getShortSize() of them.
    void analyse (const float* mono) noexcept;

private:
    SpectrumAnalyzer longAnalyzer, shortAnalyzer;
    int longSize = 0, shortSize = 0;
    std::vector<float> longWeight, merged;
};
} // namespace vintagevisual
