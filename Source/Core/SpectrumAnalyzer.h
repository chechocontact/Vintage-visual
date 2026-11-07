#pragma once

#include "Fft.h"

#include <memory>
#include <vector>

namespace vintagevisual
{
// Calibrated magnitude spectrum grouped into logarithmically spaced bands.
// A full-scale (amplitude 1.0) sine that sits on an FFT bin reads 0 dB.
// UI thread only (allocates in prepare()).
class SpectrumAnalyzer final
{
public:
    static constexpr float floorDb = -120.0f;

    void prepare (double sampleRate, int fftOrder, int numBands, double minHz = 20.0, double maxHz = 20000.0);

    // `mono` holds getFftSize() samples.
    void analyse (const float* mono) noexcept;

    int getFftSize() const noexcept { return fftSize; }
    int getNumBands() const noexcept { return (int) bandsDb.size(); }
    double getBandFrequency (int band) const noexcept { return centres[(size_t) band]; }
    const std::vector<float>& getBandsDb() const noexcept { return bandsDb; }

private:
    struct Band
    {
        double centreBin;
        int firstBin, lastBin;  // lastBin < firstBin means "interpolate at centreBin"
    };

    int fftSize = 0;
    std::unique_ptr<Fft> fft;
    std::vector<float> window, re, im, magnitude, bandsDb;
    std::vector<double> centres;
    std::vector<Band> bands;
    float gain = 1.0f;
};
} // namespace vintagevisual
