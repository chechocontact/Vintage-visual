#pragma once

namespace vintagevisual
{
struct ChannelLevels
{
    float peakDb = -120.0f;  // instant attack, falls 24 dB/s
    float holdDb = -120.0f;  // held for 1.5 s, then falls 20 dB/s (never below peakDb)
    float rmsDb = -120.0f;   // 300 ms mean-square window, dBFS
    float vuDb = -120.0f;    // VU ballistics (99% in 300 ms); 0 VU = vuReferenceDb dBFS RMS
};

// Peak / RMS / VU levels and stereo correlation from a stream of audio. UI thread; no allocation.
class LevelTracker final
{
public:
    static constexpr float floorDb = -120.0f;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setVuReferenceDb (float dbfs) noexcept { vuReferenceDb = dbfs; }

    void process (const float* left, const float* right, int numFrames) noexcept;

    const ChannelLevels& getLeft() const noexcept { return levels[0]; }
    const ChannelLevels& getRight() const noexcept { return levels[1]; }

    // -1 (out of phase) .. 0 (unrelated) .. +1 (mono). 0 while silent.
    float getCorrelation() const noexcept;

private:
    struct State
    {
        double meanSquare = 0.0, rectified = 0.0;
        double holdTimeLeft = 0.0;
    };

    double rate = 48000.0, rmsCoeff = 0.0, vuCoeff = 0.0;
    float vuReferenceDb = -18.0f;
    State state[2];
    ChannelLevels levels[2];
    double sumLL = 0.0, sumRR = 0.0, sumLR = 0.0;
};
} // namespace vintagevisual
