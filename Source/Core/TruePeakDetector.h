#pragma once

#include <vector>

namespace vintagevisual
{
// True Peak by polyphase oversampling (BS.1770-4 Annex 2 approach):
// 4x below 96 kHz, 2x below 192 kHz, none above. Windowed-sinc interpolator (49 taps at 4x).
class TruePeakDetector final
{
public:
    // Message thread only (allocates).
    void prepare (double sampleRate, int numChannels);

    // Any thread, but not concurrently with process().
    void reset() noexcept;

    // Audio thread. No allocation.
    void process (const float* const* channels, int numSamples) noexcept;

    // Maximum absolute value seen since reset(), linear.
    double getPeakLinear() const noexcept { return peak; }
    int getOversamplingFactor() const noexcept { return factor; }

private:
    static constexpr int historyLength = 13;

    int factor = 4;
    int numCh = 0;
    double peak = 0.0;
    std::vector<double> kernel;   // [phase][k]
    std::vector<double> history;  // per channel, doubled ring (2 * historyLength)
    std::vector<int> pos;
};

double linearToDb (double linear) noexcept;
} // namespace vintagevisual
