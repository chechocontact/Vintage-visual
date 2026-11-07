#pragma once

#include "KWeighting.h"
#include "TruePeakDetector.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vintagevisual
{
constexpr double silenceLufs = -120.0;

inline double energyToLufs (double energy) noexcept
{
    return energy > 0.0 ? std::fmax (silenceLufs, -0.691 + 10.0 * std::log10 (energy)) : silenceLufs;
}

struct LoudnessReadings
{
    double momentary = silenceLufs;    // 400 ms window
    double shortTerm = silenceLufs;    // 3 s window
    double integrated = silenceLufs;   // gated (-70 LUFS absolute, -10 LU relative)
    double loudnessRange = 0.0;        // LU (EBU Tech 3342)
    double truePeakDb = -120.0;        // dBTP, max since reset
};

// Fixed-memory histogram of block loudness (0.01 LU bins, -70..+10 LUFS).
// Bounded memory regardless of session length; real-time safe after construction.
class LoudnessHistogram final
{
public:
    static constexpr double minLufs = -70.0;
    static constexpr double binWidth = 0.01;
    static constexpr int numBins = 8000;

    LoudnessHistogram();

    void reset() noexcept;
    void add (double lufs) noexcept;

    // BS.1770 integrated loudness: absolute gate, then relative gate at `relativeGateLu` below the mean.
    double gatedMeanLufs (double relativeGateLu) const noexcept;

    // EBU Tech 3342 loudness range from short-term values.
    double rangeLu (double relativeGateLu) const noexcept;

private:
    int binFor (double lufs) const noexcept;
    double meanEnergyFrom (int firstBin, std::uint64_t& count) const noexcept;
    double percentileLufs (int firstBin, std::uint64_t total, double p) const noexcept;

    std::vector<std::uint32_t> counts;
    std::vector<double> energies;
};

// ITU-R BS.1770-4 / EBU R128 meter. Stereo or mono (channel weights 1.0).
class LoudnessMeter final
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int momentarySteps = 4;   // 4 x 100 ms
    static constexpr int shortTermSteps = 30;  // 30 x 100 ms

    // Message thread only (allocates). Resets all state.
    void prepare (double sampleRate, int numChannels);

    // Clears integrated, LRA and true-peak history. Not concurrent with process().
    void reset() noexcept;

    // Audio thread. No allocation. Any block size.
    void process (const float* const* channels, int numSamples) noexcept;

    // Same thread as process(); publish a copy to other threads (e.g. via atomics/snapshot).
    const LoudnessReadings& getReadings() const noexcept { return readings; }

private:
    struct ChannelFilter
    {
        Biquad shelf, highPass;
    };

    void finishStep() noexcept;
    double meanOfLatest (int count) const noexcept;

    int numChannels = 0;
    int stepSamples = 4800;
    int stepCounter = 0;
    double stepEnergy = 0.0;
    int stepsSeen = 0;
    int ringPos = 0;

    std::array<ChannelFilter, maxChannels> filters;
    std::array<double, shortTermSteps> ring {};

    LoudnessHistogram gating, range;
    TruePeakDetector truePeak;
    LoudnessReadings readings;
};
} // namespace vintagevisual
