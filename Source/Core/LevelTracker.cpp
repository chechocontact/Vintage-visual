#include "LevelTracker.h"

#include <algorithm>
#include <cmath>

namespace vintagevisual
{
namespace
{
constexpr double peakFallDbPerSecond = 24.0, holdSeconds = 1.5, holdFallDbPerSecond = 20.0;
constexpr double rectifiedToRms = 1.1107207345;  // pi / (2 * sqrt (2)): a sine then reads its RMS value

float toDb (double linear) noexcept
{
    return linear > 1.0e-6 ? (float) (20.0 * std::log10 (linear)) : LevelTracker::floorDb;
}
} // namespace

void LevelTracker::prepare (double sampleRate) noexcept
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    rmsCoeff = 1.0 - std::exp (-1.0 / (0.300 * rate));
    vuCoeff = 1.0 - std::exp (-1.0 / (0.065 * rate));
    reset();
}

void LevelTracker::reset() noexcept
{
    state[0] = state[1] = {};
    levels[0] = levels[1] = {};
    sumLL = sumRR = sumLR = 0.0;
}

void LevelTracker::process (const float* left, const float* right, int numFrames) noexcept
{
    if (numFrames <= 0) return;

    const float* inputs[2] = { left, right };
    const double dt = (double) numFrames / rate;

    for (int c = 0; c < 2; ++c)
    {
        auto& s = state[c];
        auto& l = levels[c];
        double blockPeak = 0.0;

        for (int i = 0; i < numFrames; ++i)
        {
            double x = (double) inputs[c][i];
            if (! std::isfinite (x)) x = 0.0;
            const double a = std::abs (x);
            blockPeak = std::max (blockPeak, a);
            s.meanSquare += rmsCoeff * (x * x - s.meanSquare);
            s.rectified += vuCoeff * (a * rectifiedToRms - s.rectified);
        }

        const float blockPeakDb = toDb (blockPeak);
        l.peakDb = std::max (blockPeakDb, std::max (floorDb, l.peakDb - (float) (peakFallDbPerSecond * dt)));

        if (blockPeakDb >= l.holdDb)
        {
            l.holdDb = blockPeakDb;
            s.holdTimeLeft = holdSeconds;
        }
        else if ((s.holdTimeLeft -= dt) <= 0.0)
        {
            l.holdDb = std::max (l.peakDb, std::max (floorDb, l.holdDb - (float) (holdFallDbPerSecond * dt)));
        }

        l.rmsDb = s.meanSquare > 1.0e-12 ? (float) (10.0 * std::log10 (s.meanSquare)) : floorDb;
        l.vuDb = s.rectified > 1.0e-6 ? (float) (20.0 * std::log10 (s.rectified)) - vuReferenceDb : floorDb;
    }

    for (int i = 0; i < numFrames; ++i)
    {
        const double a = std::isfinite (left[i]) ? (double) left[i] : 0.0;
        const double b = std::isfinite (right[i]) ? (double) right[i] : 0.0;
        sumLL += rmsCoeff * (a * a - sumLL);
        sumRR += rmsCoeff * (b * b - sumRR);
        sumLR += rmsCoeff * (a * b - sumLR);
    }
}

float LevelTracker::getCorrelation() const noexcept
{
    const double d = sumLL * sumRR;
    if (d < 1.0e-14) return 0.0f;
    return (float) std::clamp (sumLR / std::sqrt (d), -1.0, 1.0);
}
} // namespace vintagevisual
