#include "LoudnessMeter.h"

#include <algorithm>

namespace vintagevisual
{
// ---------------------------------------------------------------- LoudnessHistogram

LoudnessHistogram::LoudnessHistogram() : counts ((size_t) numBins, 0u), energies ((size_t) numBins)
{
    for (int i = 0; i < numBins; ++i)
    {
        const double centre = minLufs + ((double) i + 0.5) * binWidth;
        energies[(size_t) i] = std::pow (10.0, (centre + 0.691) / 10.0);
    }
}

void LoudnessHistogram::reset() noexcept
{
    std::fill (counts.begin(), counts.end(), 0u);
}

int LoudnessHistogram::binFor (double lufs) const noexcept
{
    return (int) std::floor ((lufs - minLufs) / binWidth);
}

void LoudnessHistogram::add (double lufs) noexcept
{
    if (lufs < minLufs) return;  // absolute gate
    const int idx = std::min (numBins - 1, binFor (lufs));
    ++counts[(size_t) idx];
}

double LoudnessHistogram::meanEnergyFrom (int firstBin, std::uint64_t& count) const noexcept
{
    double sum = 0.0;
    count = 0;

    for (int i = std::max (0, firstBin); i < numBins; ++i)
    {
        const auto c = counts[(size_t) i];
        if (c == 0) continue;
        sum += (double) c * energies[(size_t) i];
        count += c;
    }

    return count > 0 ? sum / (double) count : 0.0;
}

double LoudnessHistogram::percentileLufs (int firstBin, std::uint64_t total, double p) const noexcept
{
    const auto target = (std::uint64_t) ((double) (total - 1) * p + 0.5);
    std::uint64_t cumulative = 0;

    for (int i = std::max (0, firstBin); i < numBins; ++i)
    {
        cumulative += counts[(size_t) i];
        if (cumulative > target)
            return minLufs + ((double) i + 0.5) * binWidth;
    }

    return silenceLufs;
}

double LoudnessHistogram::gatedMeanLufs (double relativeGateLu) const noexcept
{
    std::uint64_t n = 0;
    const double absMean = meanEnergyFrom (0, n);
    if (n == 0) return silenceLufs;

    const double gate = energyToLufs (absMean) + relativeGateLu;
    const double gated = meanEnergyFrom (binFor (gate), n);
    return n > 0 ? energyToLufs (gated) : silenceLufs;
}

double LoudnessHistogram::rangeLu (double relativeGateLu) const noexcept
{
    std::uint64_t n = 0;
    const double absMean = meanEnergyFrom (0, n);
    if (n == 0) return 0.0;

    const int first = std::max (0, binFor (energyToLufs (absMean) + relativeGateLu));
    meanEnergyFrom (first, n);
    if (n < 2) return 0.0;

    const double lo = percentileLufs (first, n, 0.10);
    const double hi = percentileLufs (first, n, 0.95);
    return std::max (0.0, hi - lo);
}

// ---------------------------------------------------------------- LoudnessMeter

void LoudnessMeter::prepare (double sampleRate, int channels)
{
    numChannels = std::clamp (channels, 1, maxChannels);
    stepSamples = std::max (1, (int) std::lround (sampleRate * 0.1));

    const auto k = designKWeighting (sampleRate);
    for (auto& f : filters)
    {
        f.shelf.setCoefficients (k.shelf);
        f.highPass.setCoefficients (k.highPass);
    }

    truePeak.prepare (sampleRate, numChannels);
    reset();
}

void LoudnessMeter::reset() noexcept
{
    for (auto& f : filters)
    {
        f.shelf.reset();
        f.highPass.reset();
    }

    stepCounter = 0;
    stepEnergy = 0.0;
    stepsSeen = 0;
    ringPos = 0;
    ring.fill (0.0);
    gating.reset();
    range.reset();
    truePeak.reset();
    readings = {};
}

double LoudnessMeter::meanOfLatest (int count) const noexcept
{
    double sum = 0.0;
    int idx = ringPos;  // ringPos points at the next slot to write

    for (int i = 0; i < count; ++i)
    {
        idx = (idx == 0 ? shortTermSteps - 1 : idx - 1);
        sum += ring[(size_t) idx];
    }

    return sum / (double) count;
}

void LoudnessMeter::finishStep() noexcept
{
    ring[(size_t) ringPos] = stepEnergy / (double) stepSamples;
    ringPos = (ringPos + 1) % shortTermSteps;
    stepsSeen = std::min (stepsSeen + 1, shortTermSteps);  // saturate: a counter that never overflows in long sessions
    stepCounter = 0;
    stepEnergy = 0.0;

    if (stepsSeen >= momentarySteps)
    {
        readings.momentary = energyToLufs (meanOfLatest (momentarySteps));
        gating.add (readings.momentary);
        readings.integrated = gating.gatedMeanLufs (-10.0);
    }

    if (stepsSeen >= shortTermSteps)
    {
        readings.shortTerm = energyToLufs (meanOfLatest (shortTermSteps));
        range.add (readings.shortTerm);
        readings.loudnessRange = range.rangeLu (-20.0);
    }
}

void LoudnessMeter::process (const float* const* channels, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        double sum = 0.0;

        for (int c = 0; c < numChannels; ++c)
        {
            double x = (double) channels[c][i];
            if (! std::isfinite (x)) x = 0.0;

            auto& f = filters[(size_t) c];
            const double y = f.highPass.process (f.shelf.process (x));
            sum += y * y;  // channel weight 1.0 (L/R/C)
        }

        stepEnergy += sum;
        if (++stepCounter >= stepSamples)
            finishStep();
    }

    truePeak.process (channels, numSamples);
    readings.truePeakDb = linearToDb (truePeak.getPeakLinear());
}
} // namespace vintagevisual
